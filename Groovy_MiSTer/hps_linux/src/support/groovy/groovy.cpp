
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <cstring>
#include <cmath>
#include <cerrno>
#include <poll.h>
#include "../../../../protocol/groovy_transport_safety.h"
#include "../../../../protocol/groovy_reassembly.h"
#include "../../../../protocol/groovy_resource_lifecycle.h"
#include "../../../../protocol/groovy_posix_fd.h"
#include "../../../../protocol/groovy_receiver_profile.h"

/* UDP server */
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>	//ifconfig down/up

#ifdef _WIFI_MODE
#ifndef SO_BUSY_POLL_BUDGET
#define SO_BUSY_POLL_BUDGET 70 // missing from this toolchain's headers, value from linux/socket.h
#endif
#endif

#ifdef _AF_XDP
/* AF_XDP server */
#include <xdp/libxdp.h>
#include <bpf/bpf.h>
#include <xdp/xsk.h>
#include <sys/resource.h> //rlimit
#include <assert.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/ether.h>
#include <linux/if_link.h> //DRV_MODE
#endif

#include "../../hardware.h"
#include "../../shmem.h"
#include "../../spi.h"
#include "../../file_io.h"
#include "../../user_io.h"

#include "logo.h"
#include "pll.h"
#include "utils.h"

// USER_IO
static constexpr auto SCANDOUBLER_OPT = "[4:3]";
static constexpr auto ASPECT_RATIO_OPT = "[2:1]";
static constexpr auto SCALE_OPT = "[6:5]";
static constexpr auto ORIENTATION_OPT = "[10]";
static constexpr auto CROP_240P_OPT = "[11]";
static constexpr auto CROP_OFFSET_OPT = "[16:12]";
static constexpr auto CRT_H_OFFSET_OPT = "[21:17]";
static constexpr auto CRT_V_OFFSET_OPT = "[26:22]";
static constexpr auto CRT_SCALER_OPT = "[47]";
static constexpr auto CRT_SCALE_FACTOR_OPT = "[51:48]";
static constexpr auto PWM_OPT = "[37]";
static constexpr auto VOLATILE_FB_OPT = "[32]";
static constexpr auto VSYNC_OVERLAY_OPT = "[46]";
static constexpr auto AUDIO_OPT = "[34]";
static constexpr auto DESIRED_BUFFER_OPT = "[36:35]";
static constexpr auto SCREENSAVER_OPT = "[33]";
static constexpr auto ARM_CLOCK_OPT = "[45:44]";
static constexpr auto JUMBO_FRAMES_OPT = "[42]";
static constexpr auto PS2_INPUTS_OPT = "[39:38]";
static constexpr auto JOY_INPUTS_OPT = "[41:40]";
static constexpr auto VERBOSE_OPT = "[28:27]";
static constexpr auto BLIT_OPT = "[29]";
static constexpr auto AUDIO_RATE_OPT = "[53:52]";
static constexpr auto AUDIO_CHANNELS_OPT = "[55:54]";
static constexpr auto RGB_MODE_OPT = "[57:56]";
static constexpr auto LZ4_OPT = "[58]";
static constexpr auto SERVER_TYPE_OPT = "[59]";

// FPGA SPI commands
#define UIO_GET_GROOVY_STATUS     0xf0
#define UIO_SET_GROOVY_INIT       0xf2
#define UIO_SET_GROOVY_SWITCHRES  0xf3
#define UIO_SET_GROOVY_BLIT       0xf4
#define UIO_SET_GROOVY_LOGO       0xf5
#define UIO_SET_GROOVY_AUDIO      0xf6
#define UIO_SET_GROOVY_BLIT_LZ4   0xf7
#define UIO_SET_GROOVY_BLIT_FIELD_LZ4 0xf8

// FPGA DDR shared
#define BASEADDR 0x1C000000
#define HEADER_LEN 0xff
#define CHUNK 7
#define HEADER_OFFSET HEADER_LEN - CHUNK
#define FRAMEBUFFER_SIZE  (720 * 576 * 4 * 2) // RGBA 720x576 with 2 fields
#define AUDIO_SIZE (8192 * 2 * 2)             // 8192 samples with 2 16bit-channels
#define LZ4_SIZE (720 * 576 * 4)              // Estimated LZ4 MAX
#define FIELD_OFFSET 0x195000                 // 0x12fcff position for fpga (after first field)
#define AUDIO_OFFSET 0x32a000                 // 0x25f8ff position for fpga (after framebuffer)
#define LZ4_OFFSET_A 0x332000                 // 0x2678ff position for fpga (after audio)
#define LZ4_OFFSET_B 0x4c7000                 // 0x3974ff position for fpga (after lz4_offset_A)
#define LZ4_OFFSET_C 0x65c000                 // 0x2678ff position for fpga (after lz4_offset_B)
#define LZ4_OFFSET_D 0x7f1000                 // 0x3974ff position for fpga (after lz4_offset_C)
#define BUFFERSIZE FRAMEBUFFER_SIZE + AUDIO_SIZE + LZ4_SIZE + LZ4_SIZE + LZ4_SIZE + LZ4_SIZE + HEADER_LEN

// UDP server
#define UDP_PORT 32100
#define UDP_PORT_INPUTS 32101
#define UDP_PORT_GMC 32105

#ifdef _AF_XDP
// XDP server
#define XDP_BASEADDR 0x15000000
#define XDP_NUM_FRAMES 8192 			     // pot 2 (min.4096)
#define XDP_FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define RX_BATCH_SIZE groovy_receiver::receive_batch
#define INVALID_UMEM_FRAME UINT64_MAX
#endif

#define GROOVY_VERSION 1

// GroovyMiSTer protocol
#define CMD_CLOSE 1
#define CMD_INIT 2
#define CMD_SWITCHRES 3
#define CMD_AUDIO 4
#define CMD_GET_STATUS 5
#define CMD_BLIT_VSYNC 6
#define CMD_BLIT_FIELD_VSYNC 7
#define CMD_GET_VERSION 8
static_assert(CMD_CLOSE == 1 && CMD_INIT == 2 && CMD_SWITCHRES == 3 && CMD_AUDIO == 4 &&
    CMD_GET_STATUS == 5 && CMD_BLIT_VSYNC == 6 && CMD_BLIT_FIELD_VSYNC == 7 && CMD_GET_VERSION == 8,
    "Keep legacy session guard aligned with the wire commands");

//https://stackoverflow.com/questions/64318331/how-to-print-logs-on-both-console-and-file-in-c-language
#define LOG_TIMER 25
#define LOGO_TIMER 16
#define KEEP_ALIVE_FRAMES 45 * 60

static struct timespec logTS, logTS_ant, blitStart, blitStop;
static int doVerbose = 0;
static double difMs = 0;
static unsigned long logTime = 0;
static unsigned long statusLogTime = 0;
static const long LOG_MAX_BYTES = 8 * 1024 * 1024;
static FILE * fp = NULL;

#define LOG(sev,fmt, ...) do {	\
			        if (sev == 0) printf(fmt, __VA_ARGS__);	\
			        if (fp && sev <= doVerbose) { \
			        	clock_gettime(CLOCK_MONOTONIC, &logTS); \
			        	difMs = (difMs != 0) ? diff_in_ms(&logTS_ant, &logTS) : -1; \
					fprintf(fp, "[%06.3f]", difMs); \
					fprintf(fp, fmt, __VA_ARGS__);	\
                                	clock_gettime(CLOCK_MONOTONIC, &logTS_ant); \
                                } \
                           } while (0)

typedef union
{
  struct
  {
    unsigned char bit0 : 1;
    unsigned char bit1 : 1;
    unsigned char bit2 : 1;
    unsigned char bit3 : 1;
    unsigned char bit4 : 1;
    unsigned char bit5 : 1;
    unsigned char bit6 : 1;
    unsigned char bit7 : 1;
  }u;
   uint8_t byte;
} bitByte;


typedef struct {
   //frame sync
   uint32_t PoC_frame_recv;
   uint32_t PoC_frame_ddr;

   //modeline + pll -> burst 3
   uint16_t PoC_H; 	// 08
   uint8_t  PoC_HFP; 	// 10
   uint8_t  PoC_HS; 	// 11
   uint8_t  PoC_HBP; 	// 12
   uint16_t PoC_V; 	// 13
   uint8_t  PoC_VFP; 	// 15
   uint8_t  PoC_VS;     // 16
   uint8_t  PoC_VBP;    // 17

   //pll
   uint8_t  PoC_pll_M0;  // 18 High
   uint8_t  PoC_pll_M1;  // 19 Low
   uint8_t  PoC_pll_C0;  // 20 High
   uint8_t  PoC_pll_C1;  // 21 Low
   uint32_t PoC_pll_K;   // 22
   uint8_t  PoC_ce_pix;  // 26    

   uint8_t  PoC_interlaced;
   uint8_t  PoC_FB_progressive;

   double   PoC_pclock;

   uint32_t PoC_buffer_offset; // FIELD/AUDIO/LZ4 position on DDR

   //framebuffer
   uint32_t PoC_bytes_len;
   uint32_t PoC_pixels_len;
   uint32_t PoC_bytes_recv;
   uint32_t PoC_pixels_ddr;
   uint32_t PoC_field_frame;
   uint8_t  PoC_field;

   double PoC_width_time;
   uint16_t PoC_V_Total;

   //audio
   uint32_t PoC_bytes_audio_len;

   //lz4
   uint32_t PoC_bytes_lz4_len;
   uint32_t PoC_bytes_lz4_ddr;
   uint8_t  PoC_field_lz4;
   uint8_t  PoC_delta_lz4;

   //joystick
   uint32_t  PoC_joystick_keep_alive;
   uint8_t   PoC_joystick_order;
   uint32_t  PoC_joystick_map1;
   uint32_t  PoC_joystick_map2;

   char      PoC_joystick_l_analog_X1;
   char      PoC_joystick_l_analog_Y1;
   char      PoC_joystick_r_analog_X1;
   char      PoC_joystick_r_analog_Y1;

   char      PoC_joystick_l_analog_X2;
   char      PoC_joystick_l_analog_Y2;
   char      PoC_joystick_r_analog_X2;
   char      PoC_joystick_r_analog_Y2;

   //ps2
   uint32_t  PoC_ps2_keep_alive;
   uint8_t   PoC_ps2_order;
   uint8_t   PoC_ps2_keyboard_keys[ARRAY_BIT_SIZE(NUM_SCANCODES)]; //32 bytes
   uint8_t   PoC_ps2_mouse;
   uint8_t   PoC_ps2_mouse_x;
   uint8_t   PoC_ps2_mouse_y;
   uint8_t   PoC_ps2_mouse_z;

} PoC_type;

union {
    double d;
    uint64_t i;
} u;


#ifdef _AF_XDP
/* AF_XDP */
struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};

struct xsk_socket_info {
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_umem_info *umem;
	struct xsk_socket *xsk;

	groovy_safe::FramePool<XDP_NUM_FRAMES, XDP_FRAME_SIZE> frames;

	uint32_t outstanding_tx;
};

static struct xsk_umem_info *umem;
static struct xsk_socket_info *xsk_socket;
static int xsk_map_fd = -1;
static bpf_object* xdp_object = nullptr;
static int xdp_prog_fd = -1;
static unsigned xdp_ifindex = 0;
static bool xdp_attached = false;
static int packet_buffer_size;
static void *packet_buffer;
//static struct xdp_program *prog;
#endif

/* General Server variables */
static int groovyServer = 0;
static int sockfd = -1;
static struct sockaddr_in servaddr;
static struct sockaddr_in clientaddr;
static socklen_t clilen = sizeof(struct sockaddr);
static char recvbuf[65536] = { 0 };
static char sendbuf[55] = { 0 };

static int sockfdInputs = -1;
static struct sockaddr_in servaddrInputs;
static struct sockaddr_in clientaddrInputs;
static char sendbufInputs[83] = { 0 };

static int sockfdGMC = -1;
static struct sockaddr_in servaddrGMC;
static struct sockaddr_in clientaddrGMC;
static char sendbufGMC[65536] = { 0 };


#ifdef _AF_XDP
static uint32_t ip_check_1 = 0;
static uint32_t ip_check_13 = 0;
static uint32_t inputs_ip_check_9 = 0;
static uint32_t inputs_ip_check_17 = 0;
static uint32_t inputs_ip_check_37 = 0;
static uint32_t inputs_ip_check_41 = 0;

static uint32_t udp_check_1 = 0;
static uint32_t udp_check_13 = 0;
static uint32_t inputs_udp_check_9 = 0;
static uint32_t inputs_udp_check_17 = 0;
static uint32_t inputs_udp_check_37 = 0;
static uint32_t inputs_udp_check_41 = 0;
#endif

/* Logo */
static int groovyLogo = 0;
static int logoX = 0;
static int logoY = 0;
static int logoSignX = 0;
static int logoSignY = 0;
static unsigned long logoTime = 0;

static PoC_type *poc;
static uint8_t *map = 0;
static uint8_t* buffer;

static int blitCompression = 0;
static uint8_t audioRate = 0;
static uint8_t audioChannels = 0;
static uint8_t rgbMode = 0;

static groovy_safe::LegacyTransfer legacyTransfer;
static bool legacyPublishing = false;
static unsigned legacyKind = 0;
static uint32_t legacyFrame = 0;
static uint8_t legacyField = 0, legacyDelta = 0;
static uint64_t reliable_now_ms();
static bool reliable_valid_modeline(const uint8_t*);
static int isBlitting = 0;
static int isCorePriority = 0;
static int usingOldBlit = 0;

static uint8_t reliableMode = 0;
static uint16_t chunkPayloadSize = 0;
static uint32_t reliableSession = 0, reliableControl = 0;
static sockaddr_in reliablePeer = {};
static uint8_t reliableInit[groovy_wire::INIT_SIZE] = {};
static groovy_wire::Reassembly reliableVideo, reliableAudio;

static uint8_t hpsBlit = 0;
static uint16_t numBlit = 0;
static uint8_t doScreensaver = 0;
static uint8_t doPs2Inputs = 0;
static uint8_t doJoyInputs = 0;
static uint8_t doJumboFrames = 0;
#ifdef _AF_XDP
static uint8_t doXDPServer = 1;
#else        
static uint8_t doXDPServer = 0;
#endif
static uint8_t doARMClock = 0;
static uint8_t isConnected = 0;
static uint8_t isConnectedInputs = 0;
static uint8_t isConnectedGMC = 0;


/* FPGA HPS EXT STATUS */
static uint16_t fpga_vga_vcount = 0;
static uint32_t fpga_vga_frame = 0;
static uint32_t fpga_vram_pixels = 0;
static uint32_t fpga_vram_queue = 0;
static uint8_t  fpga_vram_end_frame = 0;
static uint8_t  fpga_vram_ready = 0;
static uint8_t  fpga_vram_synced = 0;
static uint8_t  fpga_vga_frameskip = 0;
static uint8_t  fpga_vga_vblank = 0;
static uint8_t  fpga_vga_f1 = 0;
static uint8_t  fpga_audio = 0;
static uint8_t  fpga_init = 0;
static uint32_t fpga_lz4_uncompressed = 0;

/* DEBUG */
/*
static uint32_t fpga_lz4_writed = 0;
static uint8_t  fpga_lz4_state = 0;
static uint8_t  fpga_lz4_run = 0;
static uint8_t  fpga_lz4_resume = 0;
static uint8_t  fpga_lz4_test1 = 0;
static uint8_t  fpga_lz4_test2 = 0;
static uint8_t  fpga_lz4_stop= 0;
static uint8_t  fpga_lz4_ABCD = 0;
static uint8_t  fpga_lz4_cmd_fskip = 0;
static uint32_t fpga_lz4_compressed = 0;
static uint32_t fpga_lz4_gravats = 0;
static uint32_t fpga_lz4_llegits = 0;
static uint32_t fpga_lz4_subframe_bytes = 0;
static uint16_t fpga_lz4_subframe_blit = 0;
*/

static inline void initDDR()
{
	memset(&buffer[0],0x00,0xff);
}


static void initVerboseFile()
{
	if (fp) fclose(fp);
	fp = fopen("/tmp/groovy.log", "at");
	if (!fp)
	{
		LOG(0, "groovy.log %s\n", "error");
		return;
	}
	struct stat stats;
    	if (fstat(fileno(fp), &stats) == -1)
    	{
        	LOG(0, "groovy.log stats %s\n", "error");
    	}
    	if (setvbuf(fp, NULL, _IOFBF, stats.st_blksize) != 0)
    	{
        	LOG(0, "setvbuf failed %s \n", "error");
    	}
    	logTime = GetTimer(1000);

}

static void groovy_FPGA_hps()
{	                   
    doVerbose = (uint8_t) user_io_status_get(VERBOSE_OPT);        
    initVerboseFile();
    
    hpsBlit = (uint8_t) user_io_status_get(BLIT_OPT); 
    doScreensaver = (uint8_t) !user_io_status_get(SCREENSAVER_OPT); 
    doPs2Inputs = (uint8_t) user_io_status_get(PS2_INPUTS_OPT);  
    doJoyInputs = (uint8_t) user_io_status_get(JOY_INPUTS_OPT);      
    doJumboFrames = (uint8_t) user_io_status_get(JUMBO_FRAMES_OPT);      
    doARMClock = (uint8_t) user_io_status_get(ARM_CLOCK_OPT);  

    LOG(0, "[HPS][doVerbose=%d hpsBlit=%d doScreenSaver=%d doPs2Inputs=%d doJoyInputs=%d doJumboFrames=%d doXDPServer=%d doARMClock=%d]\n", doVerbose, hpsBlit, doScreensaver, doPs2Inputs, doJoyInputs, doJumboFrames, doXDPServer, doARMClock);
    
    user_io_status_set(AUDIO_RATE_OPT, (uint32_t)0);
    user_io_status_set(AUDIO_CHANNELS_OPT, (uint32_t)0);
    user_io_status_set(RGB_MODE_OPT, (uint32_t)0);
    user_io_status_set(LZ4_OPT, (uint32_t)0);
}

static bool fpgaFault = false;
// Pure software invalidation: safe even after the FPGA core has disappeared.
// Do not call setClose here, which performs FPGA IO and loads the logo.
static void resetStreamSession()
{
    isConnected = isConnectedInputs = 0;
    isBlitting = isCorePriority = usingOldBlit = 0;
    numBlit = 0;
    legacyTransfer.reset(); legacyPublishing = false; legacyKind = 0;
    reliableVideo.reset(); reliableAudio.reset();
    reliableMode = 0; reliableSession = reliableControl = 0; chunkPayloadSize = 0;
    reliablePeer = {};
    memset(reliableInit, 0, sizeof(reliableInit));
    audioRate = audioChannels = rgbMode = 0; blitCompression = 0;
    if (poc) memset(poc, 0, sizeof(*poc));
}
static void failFpga(const char* operation)
{
    if (!fpgaFault) LOG(0, "[FPGA] timeout/failure: %s; stream stopped, new INIT required\n", operation);
    fpgaFault = true;
    isConnected = isBlitting = isCorePriority = 0;
}
static bool beginFpgaCommand(uint16_t command)
{
    if (fpgaFault) return false;
    EnableIO();
    if (groovy_safe::waitUntil([&] { return fpga_spi_fast(command) != 0; }, reliable_now_ms, 100))
        return true; // Caller writes/reads the command payload, then disables IO.
    DisableIO();
    failFpga("command handshake");
    return false;
}

static void groovy_FPGA_status(uint8_t isACK)
{
    if (!beginFpgaCommand(UIO_GET_GROOVY_STATUS)) return;

    fpga_vga_frame   = spi_w(0) | spi_w(0) << 16;
    fpga_vga_vcount  = spi_w(0);
    uint16_t word16  = spi_w(0);
    uint8_t word8_l  = (uint8_t)(word16 & 0x00FF);

    bitByte bits;
    bits.byte = word8_l;
    fpga_vram_ready     = bits.u.bit0;
    fpga_vram_end_frame = bits.u.bit1;
    fpga_vram_synced    = bits.u.bit2;
    fpga_vga_frameskip  = bits.u.bit3;
    fpga_vga_vblank     = bits.u.bit4;
    fpga_vga_f1         = bits.u.bit5;
    fpga_audio          = bits.u.bit6;
    fpga_init           = bits.u.bit7;

    uint8_t word8_h = (uint8_t)((word16 & 0xFF00) >> 8);
    fpga_vram_queue = word8_h; // 8b

    if (fpga_vga_vcount <= poc->PoC_interlaced) //end line
    {
	if (poc->PoC_interlaced)
	{
		if (!fpga_vga_vcount) //based on field
		{
			fpga_vga_vcount = (fpga_vga_vblank) ? poc->PoC_V_Total : 1;
		}
		else
		{
			fpga_vga_vcount = (fpga_vga_vblank) ? poc->PoC_V_Total - 1 : 2;
		}
	}
	else
	{
		fpga_vga_vcount = (fpga_vga_vblank) ? poc->PoC_V_Total : 1;
	}
    }

    if (!isACK)
    {
    	fpga_vram_queue |= spi_w(0) << 8; //24b
    	fpga_vram_pixels = spi_w(0) | spi_w(0) << 16;

	if (blitCompression || doVerbose == 3)
	{
		fpga_lz4_uncompressed  = spi_w(0) | spi_w(0) << 16;
	}

		// DEBUG 
		/*
			fpga_lz4_state = spi_w(0);
			fpga_lz4_writed = spi_w(0) | spi_w(0) << 16;

			uint16_t wordlz4 = spi_w(0);

			bits.byte = (uint8_t) wordlz4;
			fpga_lz4_cmd_fskip = bits.u.bit7;
			fpga_lz4_ABCD = (bits.u.bit5 == 0 && bits.u.bit6 == 0) ? 0 : (bits.u.bit5 == 1 && bits.u.bit6 == 0) ? 1 : (bits.u.bit5 == 0 && bits.u.bit6 == 1) ? 2 : 3;
			fpga_lz4_stop = bits.u.bit4;
			fpga_lz4_test2 = bits.u.bit3;
			fpga_lz4_test1 = bits.u.bit2;
			fpga_lz4_resume = bits.u.bit1;
                	fpga_lz4_run = bits.u.bit0;

			fpga_lz4_compressed =  spi_w(0) | spi_w(0) << 16;
			fpga_lz4_gravats = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_llegits = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_subframe_bytes = spi_w(0) | spi_w(0) << 16;
			fpga_lz4_subframe_blit = spi_w(0);
		*/	
	       
    }
    DisableIO();
}

static void groovy_FPGA_switchres()
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_SWITCHRES)) return;
    spi_w((uint16_t) poc->PoC_frame_ddr);
    spi_w((uint16_t) (poc->PoC_frame_ddr >> 16));
    DisableIO();
}

static void groovy_FPGA_blit()
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_BLIT)) return;
    spi_w(1);
    DisableIO();
}

static void groovy_FPGA_blit_field_lz4(uint32_t compressed_bytes, uint16_t field, uint8_t delta_frame)
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_BLIT_FIELD_LZ4)) return;
    bitByte bits;   
    uint8_t lz4_zone = (poc->PoC_field_lz4 == 0) ? 3 : poc->PoC_field_lz4 - 1;   
    bits.byte = lz4_zone;
    bits.u.bit2 = (field == 1) ? 1 : 0;
    bits.u.bit3 = (field == 2) ? 1 : 0;
    bits.u.bit4 = delta_frame;
    spi_w((uint16_t) bits.byte);
    spi_w((uint16_t) compressed_bytes);
    spi_w((uint16_t) (compressed_bytes >> 16));  
    DisableIO();
}

static void groovy_FPGA_init(uint8_t cmd, uint8_t audio_rate, uint8_t audio_chan, uint8_t rgb_mode)
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_INIT)) return;
    spi_w(cmd);
    bitByte bits;
    bits.byte = audio_rate;
    bits.u.bit2 = (audio_chan == 1) ? 1 : 0;
    bits.u.bit3 = (audio_chan == 2) ? 1 : 0;
    bits.u.bit4 = (rgb_mode == 1) ? 1 : 0;
    bits.u.bit5 = (rgb_mode == 2) ? 1 : 0;
    spi_w((uint16_t) bits.byte);
    DisableIO();
}

static void groovy_FPGA_logo(uint8_t cmd)
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_LOGO)) return;
    spi_w(cmd);
    DisableIO();
}

static void groovy_FPGA_audio(uint16_t samples)
{
    if (!beginFpgaCommand(UIO_SET_GROOVY_AUDIO)) return;
    spi_w(samples);
    DisableIO();
}

static bool waitFpgaReset()
{
    const bool ready = groovy_safe::waitUntil([] {
        groovy_FPGA_status(0);
        return fpgaFault || fpga_init == 0;
    }, reliable_now_ms, 100);
    if (!ready) failFpga("reset");
    return ready && !fpgaFault;
}

static void loadLogo(int logoStart)
{
	if (!doScreensaver)
	{
		return;
	}

	if (logoStart)
	{
		if (!waitFpgaReset()) return;


		buffer[0] = (1) & 0xff;
	 	buffer[1] = (1 >> 8) & 0xff;
	     	buffer[2] = (1 >> 16) & 0xff;
	  	buffer[3] = (61440) & 0xff;
	   	buffer[4] = (61440 >> 8) & 0xff;
	 	buffer[5] = (61440 >> 16) & 0xff;
		buffer[6] = (1) & 0xff;
		buffer[7] = (1 >> 8) & 0xff;

		logoTime = GetTimer(LOGO_TIMER);
	}

	if (CheckTimer(logoTime))
	{
		groovy_FPGA_status(0);
		if (fpga_vga_vcount == 241)
		{
			memset(&buffer[HEADER_OFFSET], 0x00, 184320);
		       	int z=0;
		       	int offset = (256 * logoY * 3) + (logoX * 3);
		       	for (int i=0; i<64; i++)
		       	{
		       		memcpy(&buffer[HEADER_OFFSET+offset], (char*)&logoImage[z], 192);
		       		offset += 256 * 3;
		       		z += 64 * 3;
		       	}
		       	logoTime = GetTimer(LOGO_TIMER);

		       	logoX = (logoSignX) ? logoX - 1 : logoX + 1;
		       	logoY = (logoSignY) ? logoY - 2 : logoY + 2;

		       	if (logoX >= 192 && !logoSignX)
		       	{
		       		logoSignX = !logoSignX;
		       	}

		       	if (logoY >= 176 && !logoSignY)
		       	{
		       		logoSignY = !logoSignY;
		       	}

		       	if (logoX <= 0 && logoSignX)
		       	{
		       		logoSignX = !logoSignX;
		       	}

		       	if (logoY <= 0 && logoSignY)
		       	{
		       		logoSignY = !logoSignY;
		       	}
		}
	}
}

static void groovy_FPGA_blit(uint32_t bytes, uint16_t numBlit)
{
    poc->PoC_pixels_ddr = (rgbMode == 1) ? bytes >> 2 : (rgbMode == 2) ? bytes >> 1 : bytes / 3;

    buffer[3] = (poc->PoC_pixels_ddr) & 0xff;
    buffer[4] = (poc->PoC_pixels_ddr >> 8) & 0xff;
    buffer[5] = (poc->PoC_pixels_ddr >> 16) & 0xff;

    buffer[6] = (numBlit) & 0xff;
    buffer[7] = (numBlit >> 8) & 0xff;
       
    if (poc->PoC_frame_ddr != poc->PoC_frame_recv)
    {
    	poc->PoC_frame_ddr  = poc->PoC_frame_recv;

    	buffer[0] = (poc->PoC_frame_ddr) & 0xff;
    	buffer[1] = (poc->PoC_frame_ddr >> 8) & 0xff;
    	buffer[2] = (poc->PoC_frame_ddr >> 16) & 0xff;

    	groovy_FPGA_blit();    	
    }
}

static void groovy_FPGA_blit_lz4(uint32_t bytes, uint16_t numBlit)
{
    poc->PoC_bytes_lz4_ddr = bytes;
    buffer[35] = (poc->PoC_bytes_lz4_ddr) & 0xff;
    buffer[36] = (poc->PoC_bytes_lz4_ddr >> 8) & 0xff;
    buffer[37] = (poc->PoC_bytes_lz4_ddr >> 16) & 0xff;

    buffer[38] = (numBlit) & 0xff;
    buffer[39] = (numBlit >> 8) & 0xff;

    if (poc->PoC_frame_ddr != poc->PoC_frame_recv)
    {
    	poc->PoC_frame_ddr  = poc->PoC_frame_recv;

    	buffer[32] = (poc->PoC_frame_ddr) & 0xff;
    	buffer[33] = (poc->PoC_frame_ddr >> 8) & 0xff;
    	buffer[34] = (poc->PoC_frame_ddr >> 16) & 0xff;

    	groovy_FPGA_blit_field_lz4(poc->PoC_bytes_lz4_len, poc->PoC_field, poc->PoC_delta_lz4);    
    }

}

static void setSwitchres(char *recvbuf)
{
    if (!reliable_valid_modeline((const uint8_t*)recvbuf)) {
        LOG(0, "[MODELINE] rejected invalid timing%s\n", "");
        return;
    }
    legacyTransfer.reset(); isBlitting = isCorePriority = 0;
    //modeline
    uint64_t udp_pclock_bits;
    uint16_t udp_hactive;
    uint16_t udp_hbegin;
    uint16_t udp_hend;
    uint16_t udp_htotal;
    uint16_t udp_vactive;
    uint16_t udp_vbegin;
    uint16_t udp_vend;
    uint16_t udp_vtotal;
    uint8_t  udp_interlace;

    memcpy(&udp_pclock_bits,&recvbuf[1],8);
    memcpy(&udp_hactive,&recvbuf[9],2);
    memcpy(&udp_hbegin,&recvbuf[11],2);
    memcpy(&udp_hend,&recvbuf[13],2);
    memcpy(&udp_htotal,&recvbuf[15],2);
    memcpy(&udp_vactive,&recvbuf[17],2);
    memcpy(&udp_vbegin,&recvbuf[19],2);
    memcpy(&udp_vend,&recvbuf[21],2);
    memcpy(&udp_vtotal,&recvbuf[23],2);
    memcpy(&udp_interlace,&recvbuf[25],1);

    u.i = udp_pclock_bits;
    double udp_pclock = u.d;

    poc->PoC_width_time = (double) udp_htotal * (1 / (udp_pclock * 1000)); //in ms, time to raster 1 line
    poc->PoC_V_Total = udp_vtotal;    

    poc->PoC_pixels_ddr = 0;
    poc->PoC_H = udp_hactive;
    poc->PoC_HFP = udp_hbegin - udp_hactive;
    poc->PoC_HS = udp_hend - udp_hbegin;
    poc->PoC_HBP = udp_htotal - udp_hend;
    poc->PoC_V = udp_vactive;
    poc->PoC_VFP = udp_vbegin - udp_vactive;
    poc->PoC_VS = udp_vend - udp_vbegin;
    poc->PoC_VBP = udp_vtotal - udp_vend;
    
    poc->PoC_ce_pix = (udp_pclock * 16 < 90) ? 16 : (udp_pclock * 12 < 90) ? 12 : (udp_pclock * 8 < 100) ? 8 : (udp_pclock * 6 < 100) ? 6 : 4; // we want at least 40Mhz clksys for vga scaler         
    poc->PoC_interlaced = (udp_interlace >= 1) ? 1 : 0;
    poc->PoC_FB_progressive = (udp_interlace == 0 || udp_interlace == 2) ? 1 : 0;
    
    if (usingOldBlit && !poc->PoC_FB_progressive) //old blit depends match field if fskip is activated
    {
    	groovy_FPGA_status(1);
    	poc->PoC_field_frame = poc->PoC_frame_ddr >= fpga_vga_frame ? poc->PoC_frame_ddr + 1 : fpga_vga_frame + 1;
    }
    else
    {
    	poc->PoC_field_frame = poc->PoC_frame_ddr + 1;
    }
    
    poc->PoC_field = 0;

    int M=0;
    int C=0;
    int K=0;
       
    getMCK_PLL_Fractional(udp_pclock * poc->PoC_ce_pix, M, C, K);
    poc->PoC_pll_M0 = (M % 2 == 0) ? M >> 1 : (M >> 1) + 1;
    poc->PoC_pll_M1 = M >> 1;
    poc->PoC_pll_C0 = (C % 2 == 0) ? C >> 1 : (C >> 1) + 1;
    poc->PoC_pll_C1 = C >> 1;
    poc->PoC_pll_K = K;    
    
    poc->PoC_pixels_len = poc->PoC_H * poc->PoC_V;

    if (poc->PoC_interlaced && !poc->PoC_FB_progressive)
    {
    	poc->PoC_pixels_len = poc->PoC_pixels_len >> 1;
    }

    poc->PoC_bytes_len = (rgbMode == 1) ? poc->PoC_pixels_len << 2 : (rgbMode == 2) ? poc->PoC_pixels_len << 1 : poc->PoC_pixels_len * 3;
    poc->PoC_bytes_recv = 0;
    poc->PoC_buffer_offset = 0;
    
    LOG(1,"[MODELINE][%f %d %d %d %d %d %d %d %d %s(%d)][FPGA %d %d %d %d %d %d %d %d]\n",udp_pclock,udp_hactive,udp_hbegin,udp_hend,udp_htotal,udp_vactive,udp_vbegin,udp_vend,udp_vtotal,udp_interlace?"interlace":"progressive",udp_interlace, poc->PoC_H,poc->PoC_HFP, poc->PoC_HS,poc->PoC_HBP,poc->PoC_V,poc->PoC_VFP, poc->PoC_VS,poc->PoC_VBP);
    LOG(1,"[PLL][ce_pix=%d M0=%d M1=%d C0=%d C1=%d K=%d]\n", poc->PoC_ce_pix,poc->PoC_pll_M0,poc->PoC_pll_M1,poc->PoC_pll_C0,poc->PoC_pll_C1,poc->PoC_pll_K);    
     
    //clean pixels on ddr (auto_blit)
    buffer[4] = 0x00;
    buffer[5] = 0x00;
    buffer[6] = 0x00;
    buffer[7] = 0x00;

    //modeline + pll -> burst 3
    buffer[8]  =  poc->PoC_H & 0xff;
    buffer[9]  = (poc->PoC_H >> 8);
    buffer[10] =  poc->PoC_HFP;
    buffer[11] =  poc->PoC_HS;
    buffer[12] =  poc->PoC_HBP;
    buffer[13] =  poc->PoC_V & 0xff;
    buffer[14] = (poc->PoC_V >> 8);
    buffer[15] =  poc->PoC_VFP;
    buffer[16] =  poc->PoC_VS;
    buffer[17] =  poc->PoC_VBP;

    //pll
    buffer[18] =  poc->PoC_pll_M0;
    buffer[19] =  poc->PoC_pll_M1;
    buffer[20] =  poc->PoC_pll_C0;
    buffer[21] =  poc->PoC_pll_C1;
    buffer[22] = (poc->PoC_pll_K) & 0xff;
    buffer[23] = (poc->PoC_pll_K >> 8) & 0xff;
    buffer[24] = (poc->PoC_pll_K >> 16) & 0xff;
    buffer[25] = (poc->PoC_pll_K >> 24) & 0xff;
    buffer[26] =  poc->PoC_ce_pix;
    buffer[27] =  udp_interlace;

    groovy_FPGA_switchres();
}


static void setClose()
{
    legacyTransfer.reset();
	groovy_FPGA_init(0, 0, 0, 0);
	isBlitting = 0;
	usingOldBlit = 0;
	numBlit = 0;
	blitCompression = 0;
	if (poc) memset(poc, 0, sizeof(*poc));
	initDDR();
	isConnected = 0;
	isConnectedInputs = 0;

	// load LOGO
	if (doScreensaver)
	{
		loadLogo(1);
		groovy_FPGA_init(1, 0, 0, 0);
		groovy_FPGA_blit();
		groovy_FPGA_logo(1);
		groovyLogo = 1;
	}
	
	user_io_status_set(AUDIO_RATE_OPT, (uint32_t)0);
 	user_io_status_set(AUDIO_CHANNELS_OPT, (uint32_t)0);
 	user_io_status_set(RGB_MODE_OPT, (uint32_t)0);
 	user_io_status_set(LZ4_OPT, (uint32_t)0); 	
}

#ifdef _AF_XDP
static void complete_tx(struct xsk_socket_info *xsk)
{
    if (!xsk || !xsk->outstanding_tx) return;
    sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);
    uint32_t idx = 0;
    const unsigned completed = xsk_ring_cons__peek(&xsk->umem->cq, 64, &idx);
    for (unsigned i = 0; i < completed; ++i) {
        const uint64_t addr = *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx+i);
        if (!xsk->frames.release(addr)) LOG(0, "[XDP] invalid completion address %llu\n", (unsigned long long)addr);
    }
    if (completed) {
        xsk_ring_cons__release(&xsk->umem->cq, completed);
        xsk->outstanding_tx -= std::min(xsk->outstanding_tx, completed);
    }
}
#endif

static void groovy_send_joysticks()
{
	char* sendbufPtr = (doXDPServer) ? (char*) &sendbufInputs[42] : (char*) &sendbufInputs[0];
	int len = 9;
	sendbufPtr[0] = poc->PoC_frame_ddr & 0xff;
	sendbufPtr[1] = poc->PoC_frame_ddr >> 8;
	sendbufPtr[2] = poc->PoC_frame_ddr >> 16;
	sendbufPtr[3] = poc->PoC_frame_ddr >> 24;
	sendbufPtr[4] = poc->PoC_joystick_order;
	sendbufPtr[5] = poc->PoC_joystick_map1 & 0xff;
	sendbufPtr[6] = poc->PoC_joystick_map1 >> 8;
	sendbufPtr[7] = poc->PoC_joystick_map2 & 0xff;
	sendbufPtr[8] = poc->PoC_joystick_map2 >> 8;
	if (doJoyInputs == 2)
	{
		sendbufPtr[9]  = poc->PoC_joystick_l_analog_X1;
		sendbufPtr[10] = poc->PoC_joystick_l_analog_Y1;
		sendbufPtr[11] = poc->PoC_joystick_r_analog_X1;
		sendbufPtr[12] = poc->PoC_joystick_r_analog_Y1;
		sendbufPtr[13] = poc->PoC_joystick_l_analog_X2;
		sendbufPtr[14] = poc->PoC_joystick_l_analog_Y2;
		sendbufPtr[15] = poc->PoC_joystick_r_analog_X2;
		sendbufPtr[16] = poc->PoC_joystick_r_analog_Y2;
		len += 8;
	}
	poc->PoC_joystick_keep_alive = 0;
	if (!doXDPServer)
	{
		sendto(sockfdInputs, sendbufPtr, len, MSG_CONFIRM, (struct sockaddr *)&clientaddrInputs, clilen);
	}
#ifdef _AF_XDP
	else
	{
		//struct ethhdr *eth = (struct ethhdr *)(sendbufInputs);
		struct iphdr *iph = (struct iphdr *)(sendbufInputs + sizeof(struct ethhdr));
		struct udphdr *udph = (struct udphdr *)(sendbufInputs + sizeof(struct ethhdr) + (iph->ihl * 4));
		int ret = 0;
		uint32_t tx_idx = 0;
		uint64_t addr = 0;
		complete_tx(xsk_socket);
		if (!xsk_socket->frames.available) return;
		ret = xsk_ring_prod__reserve(&xsk_socket->tx, 1, &tx_idx);
		if (ret != 1) {
			// No more transmit slots, drop the packet
			LOG(0, "[ACK_%s][Failed]\n", "STATUS");
			return;
		}
		udph->len = htons(len + sizeof(struct udphdr));
		iph->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + len);
		if (len == 9)
		{
			iph->check = inputs_ip_check_9;
			compute_udp_checksum((unsigned short *)udph, inputs_udp_check_9);	
		}
		else
		{
			iph->check = inputs_ip_check_17;
			compute_udp_checksum((unsigned short *)udph, inputs_udp_check_17);
		}		 				
		addr = xsk_socket->frames.take();
		memcpy(xsk_umem__get_data(xsk_socket->umem->buffer, addr), sendbufInputs, len + 42);
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->len = len + 42;
		xsk_ring_prod__submit(&xsk_socket->tx, 1);
		xsk_socket->outstanding_tx++;

		complete_tx(xsk_socket);
	}
#endif

}

static void groovy_send_ps2()
{
	char* sendbufPtr = (doXDPServer) ? (char*) &sendbufInputs[42] : (char*) &sendbufInputs[0];
	int len = 37;
	sendbufPtr[0] = poc->PoC_frame_ddr & 0xff;
	sendbufPtr[1] = poc->PoC_frame_ddr >> 8;
	sendbufPtr[2] = poc->PoC_frame_ddr >> 16;
	sendbufPtr[3] = poc->PoC_frame_ddr >> 24;
	sendbufPtr[4] = poc->PoC_ps2_order;
	memcpy(&sendbufPtr[5], &poc->PoC_ps2_keyboard_keys, 32);
	if (doPs2Inputs == 2)
	{
		sendbufPtr[37] = poc->PoC_ps2_mouse;
		sendbufPtr[38] = poc->PoC_ps2_mouse_x;
		sendbufPtr[39] = poc->PoC_ps2_mouse_y;
		sendbufPtr[40] = poc->PoC_ps2_mouse_z;
		len += 4;
	}
	poc->PoC_ps2_keep_alive = 0;
	if (!doXDPServer)
	{
		sendto(sockfdInputs, sendbufPtr, len, MSG_CONFIRM, (struct sockaddr *)&clientaddrInputs, clilen);
	}
#ifdef _AF_XDP
	else
	{
		//struct ethhdr *eth = (struct ethhdr *)(sendbufInputs);
		struct iphdr *iph = (struct iphdr *)(sendbufInputs + sizeof(struct ethhdr));
		struct udphdr *udph = (struct udphdr *)(sendbufInputs + sizeof(struct ethhdr) + (iph->ihl * 4));
		int ret = 0;
		uint32_t tx_idx = 0;
		uint64_t addr = 0;
		complete_tx(xsk_socket);
		if (!xsk_socket->frames.available) return;
		ret = xsk_ring_prod__reserve(&xsk_socket->tx, 1, &tx_idx);
		if (ret != 1) {
			// No more transmit slots, drop the packet
			LOG(0, "[ACK_%s][Failed]\n", "STATUS");
			return;
		}
		udph->len = htons(len + sizeof(struct udphdr));
		iph->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + len);
		if (len == 37)
		{
			iph->check = inputs_ip_check_37;
			compute_udp_checksum((unsigned short *)udph, inputs_udp_check_37);	
		}
		else
		{
			iph->check = inputs_ip_check_41;
			compute_udp_checksum((unsigned short *)udph, inputs_udp_check_41);
		}				
		addr = xsk_socket->frames.take();
		memcpy(xsk_umem__get_data(xsk_socket->umem->buffer, addr), sendbufInputs, len + 42);
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->len = len + 42;
		xsk_ring_prod__submit(&xsk_socket->tx, 1);
		xsk_socket->outstanding_tx++;

		complete_tx(xsk_socket);
	}
#endif
}

static void sendVersion()
{	
	char* sendbufPtr = (doXDPServer) ? (char*) &sendbuf[42] : (char*) &sendbuf[0];
	int flags = 0;
	flags |= MSG_CONFIRM;	
	sendbufPtr[0] = (uint8_t) GROOVY_VERSION;				
		
	if (!doXDPServer)
	{
		sendto(sockfd, sendbufPtr, 1, flags, (struct sockaddr *)&clientaddr, clilen);
	}
#ifdef _AF_XDP
	else
	{			 								
		//struct ethhdr *eth = (struct ethhdr *)(sendbuf);
		struct iphdr *iph = (struct iphdr *)(sendbuf + sizeof(struct ethhdr));
		struct udphdr *udph = (struct udphdr *)(sendbuf + sizeof(struct ethhdr) + (iph->ihl * 4));
		int ret = 0;
		uint32_t tx_idx = 0;
		uint64_t addr = 0;
		complete_tx(xsk_socket);
		if (!xsk_socket->frames.available) return;
		ret = xsk_ring_prod__reserve(&xsk_socket->tx, 1, &tx_idx);
		if (ret != 1) {
			// No more transmit slots, drop the packet
			LOG(0, "[VERSION_%s][Failed]\n", "STATUS");
			return;
		}
		iph->check = ip_check_1;
		udph->len = htons(1 + sizeof(struct udphdr));
		iph->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 1);
		compute_udp_checksum((unsigned short *)udph, udp_check_1); 		
		addr = xsk_socket->frames.take();
		memcpy(xsk_umem__get_data(xsk_socket->umem->buffer, addr), sendbuf, 43);
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->len = 43;
		xsk_ring_prod__submit(&xsk_socket->tx, 1);
		xsk_socket->outstanding_tx++;

		complete_tx(xsk_socket);
	}
#endif
}

static void sendACK(uint32_t udp_frame, uint16_t udp_vsync)
{
    if (fpgaFault) return;
	LOG(2, "[ACK_%s]\n", "STATUS");

	char* sendbufPtr = (doXDPServer) ? (char*) &sendbuf[42] : (char*) &sendbuf[0];
	int flags = 0;
	flags |= MSG_CONFIRM;
	//echo
	sendbufPtr[0] = udp_frame & 0xff;
	sendbufPtr[1] = udp_frame >> 8;
	sendbufPtr[2] = udp_frame >> 16;
	sendbufPtr[3] = udp_frame >> 24;
	sendbufPtr[4] = udp_vsync & 0xff;
	sendbufPtr[5] = udp_vsync >> 8;
	//gpu
	sendbufPtr[6] = fpga_vga_frame  & 0xff;
	sendbufPtr[7] = fpga_vga_frame  >> 8;
	sendbufPtr[8] = fpga_vga_frame  >> 16;
	sendbufPtr[9] = fpga_vga_frame  >> 24;
	sendbufPtr[10] = fpga_vga_vcount & 0xff;
	sendbufPtr[11] = fpga_vga_vcount >> 8;
	//debug bits
	bitByte bits;
	bits.byte = 0;
	bits.u.bit0 = fpga_vram_ready;
	bits.u.bit1 = fpga_vram_end_frame;
	bits.u.bit2 = fpga_vram_synced;
	bits.u.bit3 = fpga_vga_frameskip;
	bits.u.bit4 = fpga_vga_vblank;
	bits.u.bit5 = fpga_vga_f1;
	bits.u.bit6 = fpga_audio;
	bits.u.bit7 = (fpga_vram_queue > 0) ? 1 : 0;
	sendbufPtr[12] = bits.byte;


	if (!doXDPServer)
	{
		sendto(sockfd, sendbufPtr, 13, flags, (struct sockaddr *)&clientaddr, clilen);
	}
#ifdef _AF_XDP
	else
	{
		//struct ethhdr *eth = (struct ethhdr *)(sendbuf);
		struct iphdr *iph = (struct iphdr *)(sendbuf + sizeof(struct ethhdr));
		struct udphdr *udph = (struct udphdr *)(sendbuf + sizeof(struct ethhdr) + (iph->ihl * 4));
		int ret = 0;
		uint32_t tx_idx = 0;
		uint64_t addr = 0;
		complete_tx(xsk_socket);
		if (!xsk_socket->frames.available) return;
		ret = xsk_ring_prod__reserve(&xsk_socket->tx, 1, &tx_idx);
		if (ret != 1) {
			// No more transmit slots, drop the packet
			LOG(0, "[ACK_%s][Failed]\n", "STATUS");
			return;
		}		
		iph->check = ip_check_13;
		udph->len = htons(13 + sizeof(struct udphdr));
		iph->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 13);		
		compute_udp_checksum((unsigned short *)udph, udp_check_13); 						
		addr = xsk_socket->frames.take();
		memcpy(xsk_umem__get_data(xsk_socket->umem->buffer, addr), sendbuf, 55);
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->addr = addr;
		xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->len = 55;
		xsk_ring_prod__submit(&xsk_socket->tx, 1);
		xsk_socket->outstanding_tx++;

		complete_tx(xsk_socket);
	}
#endif
	if (poc->PoC_joystick_keep_alive >= KEEP_ALIVE_FRAMES)
	{
		LOG(2, "[JOY_ACK][%s]\n", "KEEP_ALIVE");
		groovy_send_joysticks();
	}

	if (poc->PoC_ps2_keep_alive >= KEEP_ALIVE_FRAMES)
	{
		LOG(2, "[KBD_ACK][%s]\n", "KEEP_ALIVE");
		groovy_send_ps2();
	}
}

static bool setInit(uint8_t compression, uint8_t audio_rate, uint8_t audio_chan, uint8_t rgb_mode, uint8_t reliable_mode, uint16_t chunk_payload_size)
{
    isConnected = 0;
    fpgaFault = false;
    groovy_FPGA_init(0, 0, 0, 0); // A fresh/retried session must reset even without screensaver.
    if (fpgaFault) return false;
    legacyTransfer.reset();
	difMs = 0;
	fpga_lz4_uncompressed = 0;
	blitCompression = (compression <= 1) ? compression : 0;
	audioRate = (audio_rate <= 3) ? audio_rate : 0;
	audioChannels = (audio_chan <= 2) ? audio_chan : 0;
	rgbMode = (rgb_mode <= 2) ? rgb_mode : 0;
	reliableMode = reliable_mode;
	chunkPayloadSize = chunk_payload_size;
	if (!poc) poc = (PoC_type *) calloc(1, sizeof(PoC_type));
	else memset(poc, 0, sizeof(*poc));
    if (!poc) { failFpga("state allocation"); return false; }
	initDDR();
	isBlitting = 0;
	usingOldBlit = 0;
	numBlit = 0;


	char hoststr[NI_MAXHOST];
	char portstr[NI_MAXSERV];
	// load LOGO
	if (doScreensaver)
	{
		groovy_FPGA_init(0, 0, 0, 0);
		groovy_FPGA_logo(0);
		groovyLogo = 0;
	}

	if (!isConnected)
	{
		getnameinfo((struct sockaddr *)&clientaddr, clilen, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
		LOG(1,"[Connected][%s][%s:%s]\n", (doXDPServer) ? "XDP" : "UDP", hoststr, portstr);
		isConnected = 1;
	}

	if (doPs2Inputs || doJoyInputs)
  	{
  		int len = 0;
  		if (!doXDPServer)
  		{
  			len = recvfrom(sockfdInputs, recvbuf, 1, 0, (struct sockaddr *)&clientaddrInputs, &clilen);
  		}
		
  		if (len > 0 || isConnectedInputs)
  		{
			getnameinfo((struct sockaddr *)&clientaddrInputs, clilen, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
			LOG(1,"[Inputs][%s:%s]\n", hoststr, portstr);
  			isConnectedInputs = 1;
  		} 		
  	}

	if (!waitFpgaReset()) return false;

 	user_io_status_set(AUDIO_RATE_OPT, (uint32_t)audioRate);
 	user_io_status_set(AUDIO_CHANNELS_OPT, (uint32_t)audioChannels);
 	user_io_status_set(RGB_MODE_OPT, (uint32_t)rgbMode);
 	user_io_status_set(LZ4_OPT, (uint32_t)blitCompression);
 	
	groovy_FPGA_init(1, audioRate, audioChannels, rgbMode);
    return !fpgaFault;
}

static void setBlit(uint32_t udp_frame, uint8_t udp_field, uint32_t udp_lz4_size, uint8_t udp_frame_delta)
{
    if (!isConnected || fpgaFault || !poc || !poc->PoC_bytes_len || poc->PoC_bytes_len > groovy_wire::MAX_VIDEO ||
        udp_field > 2 || udp_frame_delta > 1 ||
        (blitCompression && (!udp_lz4_size || udp_lz4_size > groovy_wire::MAX_VIDEO))) return;
    if (!reliableMode && !legacyPublishing && (blitCompression || !udp_frame_delta)) {
        legacyKind = 1; legacyFrame = udp_frame; legacyField = udp_field; legacyDelta = udp_frame_delta;
        if (legacyTransfer.begin(blitCompression ? udp_lz4_size : poc->PoC_bytes_len,
                groovy_wire::MAX_VIDEO, 1472, reliable_now_ms())) {
            isBlitting = 1;
            isCorePriority = 0; // Return to the UI even while packets are missing.
        }
        return;
    }
	poc->PoC_frame_recv = udp_frame;
	poc->PoC_bytes_recv = (!blitCompression && udp_frame_delta) ? poc->PoC_bytes_len : 0; //on raw, only duplicated frame supported
	poc->PoC_bytes_lz4_ddr = 0;
	poc->PoC_bytes_lz4_len = (blitCompression) ? udp_lz4_size : 0;	
	if (udp_field == 2)
	{
		poc->PoC_field = (!poc->PoC_FB_progressive) ? (poc->PoC_frame_recv + poc->PoC_field_frame) % 2 : 0;	
	}
	else
	{
		poc->PoC_field = (!udp_field && !poc->PoC_FB_progressive) ? 1 : 0;
	}

	if (blitCompression)
	{
		poc->PoC_buffer_offset = (poc->PoC_field_lz4 == 3) ? LZ4_OFFSET_D : (poc->PoC_field_lz4 == 2) ? LZ4_OFFSET_C : (poc->PoC_field_lz4 == 1) ? LZ4_OFFSET_B : LZ4_OFFSET_A;  
		poc->PoC_field_lz4 = (poc->PoC_field_lz4 == 3) ? 0 : poc->PoC_field_lz4 + 1;
		poc->PoC_delta_lz4 = udp_frame_delta;
	}
	else
	{
		poc->PoC_buffer_offset = (!poc->PoC_FB_progressive && poc->PoC_field) ? FIELD_OFFSET : 0;
		poc->PoC_field_lz4 = 0;
	}
	
	poc->PoC_joystick_order = 0;
	poc->PoC_ps2_order = 0;

	if (isConnectedInputs && doJoyInputs)
	{
		poc->PoC_joystick_keep_alive++;
	}

	if (isConnectedInputs && doPs2Inputs)
	{
		poc->PoC_ps2_keep_alive++;
	}

	
	isBlitting = (!blitCompression && udp_frame_delta) ? 0 : 1;
	isCorePriority =  (!blitCompression && udp_frame_delta) ? 0 : 1;
	numBlit = (!blitCompression && udp_frame_delta) ? 1 : 0;
			
	if (!hpsBlit || (!blitCompression && udp_frame_delta)) //ASAP fpga starts to poll ddr
	{
		if (blitCompression)
		{
			groovy_FPGA_blit_lz4(0, 0);
		}
		else
		{
			groovy_FPGA_blit(poc->PoC_bytes_recv, numBlit);
		}
	}
	else
	{
		poc->PoC_pixels_ddr = 0;
		poc->PoC_bytes_lz4_ddr = 0;
	}		

	if (doVerbose > 0 && doVerbose < 3)
	{
		groovy_FPGA_status(0);
		LOG(1, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);
	}

	if (!doVerbose && !fpga_vram_synced)
 	{
 		groovy_FPGA_status(0);
 		LOG(0, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);
 	}	
 	
 	if (!poc->PoC_bytes_recv)
 	{
 		clock_gettime(CLOCK_MONOTONIC, &blitStart);
 	}		
}

static void setBlitAudio(uint16_t udp_bytes_samples)
{
    if (!isConnected || fpgaFault || !poc || !poc->PoC_bytes_len || !audioChannels || !udp_bytes_samples || udp_bytes_samples > AUDIO_SIZE ||
        udp_bytes_samples % (2*audioChannels)) return;
    if (!reliableMode && !legacyPublishing) {
        legacyKind = 2;
        if (legacyTransfer.begin(udp_bytes_samples, AUDIO_SIZE,
                1472, reliable_now_ms())) {
            isBlitting = 2; isCorePriority = 0;
        }
        return;
    }
	poc->PoC_bytes_audio_len = udp_bytes_samples;
	poc->PoC_buffer_offset = AUDIO_OFFSET;
	poc->PoC_bytes_recv = 0;

	isBlitting = 2;
	isCorePriority = 1;
}

static void setBlitRawAudio(uint16_t len)
{
	poc->PoC_bytes_recv += len;
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_audio_len) ? 0 : 2;

	LOG(2, "[DDR_AUDIO][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_audio_len);

	if (isBlitting == 0)
	{
		uint16_t sound_samples = (audioChannels == 0) ? 0 : (audioChannels == 1) ? poc->PoC_bytes_audio_len >> 1 : poc->PoC_bytes_audio_len >> 2;
		groovy_FPGA_audio(sound_samples);
		poc->PoC_buffer_offset = 0;
		isCorePriority = 0;
	}
}

static void setBlitRaw(uint16_t len)
{
	poc->PoC_bytes_recv += len;
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_len) ? 0 : 1;

       	if (!hpsBlit) //ASAP
       	{
       		numBlit++;
		groovy_FPGA_blit(poc->PoC_bytes_recv, numBlit);
		LOG(2, "[ACK_BLIT][(%d) px=%d/%d %d/%d]\n", numBlit, poc->PoC_pixels_ddr, poc->PoC_pixels_len, poc->PoC_bytes_recv, poc->PoC_bytes_len);
		//groovy_FPGA_status(0);
       		//LOG(1, "[ACK_STATUS][DDR fr=%d bl=%d][GPU vc=%d fr=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 state_1=%d inf=%d wr=%d, run=%d resume=%d t1=%d t2=%d cmd_fskip=%d stop=%d AB=%d com=%d grav=%d lleg=%d, sub=%d blit=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_vcount, fpga_vga_frame, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_state, fpga_lz4_uncompressed, fpga_lz4_writed, fpga_lz4_run, fpga_lz4_resume, fpga_lz4_test1, fpga_lz4_test2, fpga_lz4_cmd_fskip, fpga_lz4_stop, fpga_lz4_ABCD, fpga_lz4_compressed, fpga_lz4_gravats, fpga_lz4_llegits, fpga_lz4_subframe_bytes, fpga_lz4_subframe_blit);
       	}
       	else
       	{
       		LOG(2, "[DDR_BLIT][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_len);
       	}

        if (isBlitting == 0)
        {
        	isCorePriority = 0;
        	if (poc->PoC_pixels_ddr < poc->PoC_pixels_len)
        	{
        		numBlit++;
			groovy_FPGA_blit(poc->PoC_bytes_recv, numBlit);
			LOG(2, "[ACK_BLIT][(%d) px=%d/%d %d/%d]\n", numBlit, poc->PoC_pixels_ddr, poc->PoC_pixels_len, poc->PoC_bytes_recv, poc->PoC_bytes_len);
        	}
        	poc->PoC_buffer_offset = 0;		
		clock_gettime(CLOCK_MONOTONIC, &blitStop);        	
        	double difBlit = diff_in_ms(&blitStart, &blitStop);
        	LOG(1, "[DDR_BLIT][TOTAL %06.3f][(%d) Bytes=%d]\n", difBlit, numBlit, poc->PoC_bytes_len);
        }
}

static void setBlitLZ4(uint16_t len)
{
	poc->PoC_bytes_recv += len;
	isBlitting = (poc->PoC_bytes_recv >= poc->PoC_bytes_lz4_len) ? 0 : 1;

	if (!hpsBlit) //ASAP
       	{
       		numBlit++;
		groovy_FPGA_blit_lz4(poc->PoC_bytes_recv, numBlit);
		LOG(2, "[ACK_BLIT][(%d) %d/%d]\n", numBlit, poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);				
       	}
       	else
       	{
       		LOG(2, "[LZ4_BLIT][%d/%d]\n", poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);
       	}

	if (isBlitting == 0)
        {
        	isCorePriority = 0;
        	if (poc->PoC_bytes_lz4_ddr < poc->PoC_bytes_lz4_len)
        	{
        		numBlit++;
			groovy_FPGA_blit_lz4(poc->PoC_bytes_recv, numBlit);
			LOG(2, "[ACK_BLIT][(%d) %d/%d]\n", numBlit, poc->PoC_bytes_recv, poc->PoC_bytes_lz4_len);
        	}
        	poc->PoC_buffer_offset = 0;		
		clock_gettime(CLOCK_MONOTONIC, &blitStop);        	
        	double difBlit = diff_in_ms(&blitStart, &blitStop);
		LOG(1, "[LZ4_BLIT][TOTAL %06.3f][(%d) Bytes=%d]\n", difBlit, numBlit, poc->PoC_bytes_lz4_len);
        }
}

static void groovy_map_ddr()
{
    	int pagesize = sysconf(_SC_PAGE_SIZE);
    	if (pagesize==0) pagesize=4096;
    	int offset = BASEADDR;
    	int map_start = offset & ~(pagesize - 1);
    	int map_off = offset - map_start;
    	int num_bytes=BUFFERSIZE;

    	map = (uint8_t*)shmem_map(map_start, num_bytes+map_off);
    	buffer = map + map_off;

    	initDDR();
	if (!poc) poc = (PoC_type *) calloc(1, sizeof(PoC_type));
	else memset(poc, 0, sizeof(*poc));

    	isCorePriority = 0;
    	isBlitting = 0;
}

#ifdef _AF_XDP
static bool closeOnExec(int fd)
{
    return groovy_safe::closeOnExec(fd);
}

static bool cleanupXdp()
{
    return groovy_safe::releaseXdpResources(
        [] {
            if (xdp_attached) {
                bpf_xdp_set_link_opts opts{};
                opts.sz = sizeof(opts); opts.old_fd = xdp_prog_fd;
                const int result = bpf_set_link_xdp_fd_opts(xdp_ifindex, -1, XDP_FLAGS_DRV_MODE, &opts);
                // EEXIST means somebody replaced our program; do not detach theirs.
                if (result && result != -ENOENT && result != -EEXIST) {
                    LOG(0, "[XDP][STOP] detach failed %d; core change cancelled\n", result);
                    return false;
                }
                xdp_attached = false;
            }
            if (xsk_map_fd >= 0) {
                const uint32_t queue = 0;
                if (bpf_map_delete_elem(xsk_map_fd, &queue) && errno != ENOENT) {
                    LOG(0, "[XDP][STOP] map removal failed %d; core change cancelled\n", errno);
                    return false;
                }
            }
            return true;
        },
        [] {
            if (xsk_socket) {
                if (xsk_socket->xsk) xsk_socket__delete(xsk_socket->xsk);
                free(xsk_socket); xsk_socket = nullptr;
            }
            sockfd = -1; // The XSK/UMEM owns this descriptor; never close twice.
        },
        [] {
            if (umem) {
                const int result = xsk_umem__delete(umem->umem);
                if (result) {
                    LOG(0, "[XDP][STOP] UMEM still referenced (%d); memory retained\n", result);
                    return false;
                }
                free(umem); umem = nullptr;
            }
            return true;
        },
        [] {
            if (packet_buffer) shmem_unmap(packet_buffer, packet_buffer_size);
            packet_buffer = nullptr; packet_buffer_size = 0;
        },
        [] {
            if (xdp_object) bpf_object__close(xdp_object);
            xdp_object = nullptr; xdp_prog_fd = xsk_map_fd = -1; xdp_ifindex = 0;
        });
}

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
	struct xsk_umem_info *umem;
	int ret;

	umem = (xsk_umem_info*) calloc(1, sizeof(*umem));
	if (!umem)
	{
		LOG(0, "[XDP][configure_xsk_umem:calloc][%s]\n", "error");
		return NULL;
	}

	ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq, NULL);
	if (ret)
	{
		LOG(0, "[XDP][configure_xsk_umem:xsk_umem__create][%s]\n", "error");
		free(umem);
		return NULL;
	}
    if (!closeOnExec(xsk_umem__fd(umem->umem))) {
        xsk_umem__delete(umem->umem); free(umem); return nullptr;
    }
	umem->buffer = buffer;
	return umem;
}

static inline void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
    if (!xsk->frames.releasePacket(frame)) LOG(0, "[XDP] invalid RX address %llu\n", (unsigned long long)frame);
}
static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    return xsk->frames.take();
}

static struct xsk_socket_info *xsk_configure_socket(struct xsk_umem_info *umem)
{
	struct xsk_socket_config xsk_cfg;
	struct xsk_socket_info *xsk_info;
	uint32_t idx;
	int i;
	int ret;
//	int sock_opt;

	xsk_info = (xsk_socket_info*) calloc(1, sizeof(*xsk_info));
	if (!xsk_info)
	{
		LOG(0,"[XDP][xsk_info][%s]\n", "error");
		goto xsk_socket_error;
	}
	xsk_info->umem = umem;
	xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;	//best value				
	xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;	//best value
	
	xsk_cfg.xdp_flags = XDP_FLAGS_DRV_MODE;		
	//xsk_cfg.bind_flags = XDP_USE_NEED_WAKEUP | XDP_ZEROCOPY; ////zc + recvfrom/sendto UNSTABLE			
	//xsk_cfg.bind_flags &= ~XDP_USE_NEED_WAKEUP;		
	xsk_cfg.bind_flags = XDP_COPY;     		
	xsk_cfg.bind_flags |= XDP_USE_NEED_WAKEUP;
	xsk_cfg.libbpf_flags = XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD;
	ret = xsk_socket__create(&xsk_info->xsk, "eth0", 0, umem->umem, &xsk_info->rx, &xsk_info->tx, &xsk_cfg);
	if (ret)
	{
		LOG(0,"[XDP][xsk_socket__create][error %d]\n", ret);
		goto xsk_socket_error;
	}

	ret = xsk_socket__update_xskmap(xsk_info->xsk, xsk_map_fd);
	if (ret)
	{
		LOG(0,"[XDP][xsk_socket__update_xskmap][error %d]\n", ret);
		goto xsk_socket_error;
	}
	if (!closeOnExec(xsk_socket__fd(xsk_info->xsk))) goto xsk_socket_error;

	xsk_info->frames.init();

	/* Stuff the receive path with buffers, we assume we have enough */
	ret = xsk_ring_prod__reserve(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);

	if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
	{
		LOG(0,"[XDP][XSK_RING_PROD__DEFAULT_NUM_DESCS][error %d]\n", ret);
		goto xsk_socket_error;
	}

	for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
	{
		*xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) = xsk_alloc_umem_frame(xsk_info);
	}
	xsk_ring_prod__submit(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);


	// Set socket options (busy poll) Warning: fails sends with XDP_COPY?
	sockfd = xsk_socket__fd(xsk_info->xsk);
/*       
	sock_opt = 20;
        ret = setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL, (void *)&sock_opt, sizeof(sock_opt));
        if (ret < 0)
        {
          	LOG(0,"[XDP][SO_BUSY_POLL][error %d]\n", ret);
          	goto xsk_socket_error;
        }
       	sock_opt = 1;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_PREFER_BUSY_POLL, (void *)&sock_opt, sizeof(sock_opt));
        if (ret < 0)
        {
           	LOG(0,"[XDP][SO_PREFER_BUSY_POLL][error %d]\n", ret);
		goto xsk_socket_error;
        }

        sock_opt = 256;
        ret = setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, (void *)&sock_opt, sizeof(sock_opt));
        if (ret < 0)
        {
          	LOG(0,"[XDP][SO_BUSY_POLL_BUDGET][error %d]\n", ret);
           	goto xsk_socket_error;
        }
*/			
	
	return xsk_info;

xsk_socket_error:
	if (xsk_info) {
		if (xsk_info->xsk) xsk_socket__delete(xsk_info->xsk);
		free(xsk_info);
	}
	return NULL;
}
#endif

static int setMTU()
{
    const bool ethernet = getNet(1) != nullptr;
    const bool wireless = getNet(2) != nullptr;
    if (!groovy_receiver::profile.networkReady(ethernet, wireless)) return -1;
    // Wi-Fi must not reconfigure/require eth0 just to open its UDP socket.
    if (!groovy_receiver::profile.configureEthernet(ethernet)) return 0;
    // This temporary ioctl socket must never overwrite the live server/XSK fd.
    struct LocalSocket {
        int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
        ~LocalSocket() { if (fd >= 0) close(fd); }
    } control;
    if (control.fd < 0) return -1;
    ifreq ifr{};
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ);
    if (ioctl(control.fd, SIOCGIFMTU, &ifr) < 0) return -1;
    if ((doJumboFrames && ifr.ifr_mtu == 1500) || (!doJumboFrames && ifr.ifr_mtu != 1500)) {
        if (ioctl(control.fd, SIOCGIFFLAGS, &ifr) < 0) return -1;
        const short originalFlags = ifr.ifr_flags;
        ifr.ifr_flags = originalFlags & ~IFF_UP;
        if (ioctl(control.fd, SIOCSIFFLAGS, &ifr) < 0) return -1;
        ifr.ifr_mtu = doJumboFrames ? 3800 : 1500;
        if (ioctl(control.fd, SIOCSIFMTU, &ifr) < 0) {
            ifr.ifr_mtu = 1500; doJumboFrames = 0;
            ioctl(control.fd, SIOCSIFMTU, &ifr);
        }
        ifr.ifr_flags = originalFlags | IFF_UP;
        if (ioctl(control.fd, SIOCSIFFLAGS, &ifr) < 0)
            LOG(0, "[ETH0] could not restore interface after MTU change: %d\n", errno);
        return -1; // Let the interface settle before retrying server startup.
    }
    return 0;
}

#ifdef _AF_XDP
static void groovy_xdp_server_init()
{		
	int err = 0;	
	int pagesize, map_start, map_off;
	struct bpf_map *map;
	struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};	
	struct bpf_prog_load_attr prog_load_attr;
	//struct xdp_multiprog *mp = NULL;	

    if (!cleanupXdp()) return;
	if (setMTU() < 0)
	{
		goto init_error_xdp;	
	}	
	
	setRXAffinity(0);

	// load ebpf program and attach to kernel eth0
	memset(&prog_load_attr, 0, sizeof(struct bpf_prog_load_attr));
	prog_load_attr.prog_type = BPF_PROG_TYPE_XDP;
	prog_load_attr.file = "/usr/lib/arm-linux-gnueabihf/bpf/groovy_xdp_kern.o";
	if (bpf_prog_load_xattr(&prog_load_attr, &xdp_object, &xdp_prog_fd))
	{
		LOG(0, "[XDP][bpf_prog_load_xattr][%s]\n", "error");
		goto init_error_xdp;
	}
	if (xdp_prog_fd < 0) {
		LOG(0, "[XDP][bpf_prog_load_xattr][%d]\n", xdp_prog_fd);
		goto init_error_xdp;
	}
    xdp_ifindex = if_nametoindex("eth0");
	err = bpf_set_link_xdp_fd(xdp_ifindex, xdp_prog_fd, XDP_FLAGS_DRV_MODE);
	//err = bpf_set_link_xdp_fd(if_nametoindex("eth0"), prog_fd, XDP_FLAGS_SKB_MODE);
	
	if (err < 0)
	{
		LOG(0, "[XDP][bpf_set_link_xdp_fd][error %d]\n", err);
		goto init_error_xdp;
	}
    xdp_attached = true;
	map = bpf_object__find_map_by_name(xdp_object, "xsks_map");
    if (!map) goto init_error_xdp;

	// with dispatcher
	/*
	prog = xdp_program__open_file("/usr/lib/arm-linux-gnueabihf/bpf/groovy_xdp_kern.o", "xdp_groovymister", NULL);
	err = libxdp_get_error(prog);
	if (err)
	{
		LOG(0, "[XDP][xdp_program__open_file][error %d]\n", err);
		goto init_error_xdp;
	}
	// attach using native mode driver stmmac
	err = xdp_program__attach(prog, if_nametoindex("eth0"), XDP_MODE_NATIVE, 0);
	if (err)
	{
		if (err != -16) //prev.attached
		{
			LOG(0, "[XDP][xdp_program__attach][error %d]\n", err);
			goto init_error_xdp;
		}
		else
		{
			LOG(0, "[XDP][xdp_program__attach][%s]\n", "skip");
		}
	}
	// load maps
	map = bpf_object__find_map_by_name(xdp_program__bpf_obj(prog), "xsks_map");
	*/
	xsk_map_fd = bpf_map__fd(map);
	if (xsk_map_fd < 0)
	{
		LOG(0, "[XDP][bpf_map__fd[error %d]\n", xsk_map_fd);
		goto init_error_xdp;
	}
	// no limit memory alloc
	err = setrlimit(RLIMIT_MEMLOCK, &rlim);
	if (err)
	{
		LOG(0, "[XDP][setrlimit][error %d]\n", err);
		goto init_error_xdp;
	}
	// allocate map for umem
	pagesize = sysconf(_SC_PAGE_SIZE);
    	if (pagesize==0) pagesize=4096;
    	map_start = XDP_BASEADDR & ~(pagesize - 1);
    	map_off = XDP_BASEADDR - map_start;
    	packet_buffer_size = (XDP_NUM_FRAMES * XDP_FRAME_SIZE) + map_off;
    	packet_buffer = shmem_map_private(map_start, packet_buffer_size);
        if (!packet_buffer || packet_buffer == (void *)-1)
    	{
    		LOG(0, "[XDP][mmap umem][%s]\n", "error");
        packet_buffer = nullptr;
    		goto init_error_xdp;
    	}
    	// Initialize shared packet_buffer for umem usage
	umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
	if (umem == NULL)
	{
		LOG(0, "[XDP][configure_xsk_umem][%s]\n", "error");
		goto init_error_xdp;
	}
	// Open and configure the AF_XDP (xsk) socket
	xsk_socket = xsk_configure_socket(umem);
	if (xsk_socket == NULL)
	{
		LOG(0, "[XDP][xsk_configure_socket][%s]\n", "error");
		goto init_error_xdp;
	}

	LOG(0, "[XDP][STARTED][%d]\n", GROOVY_VERSION);

	groovyServer = 2;
	return;

init_error_xdp:
    cleanupXdp();
	groovyServer = 1;
}
#endif

static void groovy_udp_server_init()
{
	int ret = 0;
	int flags, size, beTrueAddr, tos;
#ifdef _WIFI_MODE
	int busyPoll;
#endif
	
	if (setMTU() < 0)
	{
		goto init_error_udp;	
	}
	
	if (groovy_receiver::profile.configureEthernet(getNet(1) != nullptr)) setRXAffinity(0);
	
	sockfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    	if (sockfd < 0)
    	{
    		LOG(0, "[UDP][socket error %d]\n", sockfd);
    		goto init_error_udp;
    	}
    	
    	memset(&servaddr, 0, sizeof(servaddr));
    	servaddr.sin_family = AF_INET;
    	servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    	servaddr.sin_port = htons(UDP_PORT);

        // Non blocking socket
    	flags = fcntl(sockfd, F_GETFL, 0);
    	if (flags < 0)
    	{
      		LOG(0, "[UDP][get falg][error %d]\n", flags);
      		goto init_error_udp;
    	}
    	flags |= O_NONBLOCK;
    	ret = fcntl(sockfd, F_SETFL, flags);
    	if (ret < 0)
    	{
    		LOG(0, "[UDP][set nonblock fail][error %d]\n", ret);
       		goto init_error_udp;
    	}

	// Settings
	size = groovy_receiver::profile.udp_buffer;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_RCVBUFFORCE, (void*)&size, sizeof(size));
        if (ret < 0)
        {
        	LOG(0, "[UDP][SO_RCVBUFFORCE][error %d]\n", ret);
        	goto init_error_udp;
        }
	beTrueAddr = 1;
	ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (void*)&beTrueAddr,sizeof(beTrueAddr));
	if (ret < 0)
	{
        	LOG(0, "[UDP][SO_REUSEADDR][error %d]\n", ret);
        	goto init_error_udp;
        }
        tos = groovy_receiver::profile.ip_tos;
        ret = setsockopt(sockfd, IPPROTO_IP, IP_TOS, (char*)&tos,sizeof(tos));
        if (ret < 0)
	{
        	LOG(0, "[UDP][IP_TOS][error %d]\n", ret);
        	goto init_error_udp;
        }
#ifdef _WIFI_MODE
        busyPoll = 20;
        ret = setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL, (void *)&busyPoll, sizeof(busyPoll));
        if (ret < 0)
        {
		LOG(0,"[UDP][SO_BUSY_POLL][optional setting unavailable %d]\n", errno);
        }
        busyPoll = 256;
        ret = setsockopt(sockfd, SOL_SOCKET, SO_BUSY_POLL_BUDGET, (void *)&busyPoll, sizeof(busyPoll));
        if (ret < 0)
        {
		LOG(0,"[UDP][SO_BUSY_POLL_BUDGET][optional setting unavailable %d]\n", errno);
        }
#endif
        ret = bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    	if (ret < 0)
    	{
    		LOG(0, "[UDP][bind][error %d]\n", ret);
    		goto init_error_udp;
    	}

	LOG(0, "[UDP][STARTED][%d]\n", GROOVY_VERSION);
	groovyServer = 2;
	return;

init_error_udp:
    if (sockfd >= 0) close(sockfd);
    sockfd = -1;
	groovyServer = 1;

}

static void groovy_udp_server_init_inputs()
{
	int ret = 0;
	int flags, beTrueAddr;
	sockfdInputs = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    	if (sockfdInputs < 0)
    	{
    		LOG(0, "[UDP][socketInputs][error %d]\n", sockfdInputs);
    		goto inputs_error;
    	}

    	memset(&servaddrInputs, 0, sizeof(servaddrInputs));
    	servaddrInputs.sin_family = AF_INET;
    	servaddrInputs.sin_addr.s_addr = htonl(INADDR_ANY);
    	servaddrInputs.sin_port = htons(UDP_PORT_INPUTS);

        // Non blocking socket
    	flags = fcntl(sockfdInputs, F_GETFL, 0);
    	if (flags < 0)
    	{
      		LOG(0, "[UDP][get falg inputs][error %d]\n", flags);
      		goto inputs_error;
    	}
    	flags |= O_NONBLOCK;
    	ret = fcntl(sockfdInputs, F_SETFL, flags);
    	if (ret < 0)
    	{
       		LOG(0, "[UDP][set nonblock inputs fail][error %d]\n", ret);
       		goto inputs_error;
    	}

	beTrueAddr = 1;
	ret = setsockopt(sockfdInputs, SOL_SOCKET, SO_REUSEADDR, (void*)&beTrueAddr,sizeof(beTrueAddr));
	if (ret < 0)
	{
        	LOG(0, "[UDP][SO_REUSEADDR inputs][error %d]\n", ret);
        	goto inputs_error;
        }
        ret = setsockopt(sockfdInputs, IPPROTO_IP, IP_TOS, (char*)&beTrueAddr,sizeof(beTrueAddr));
        if (ret < 0)
	{
        	LOG(0, "[UDP][IP_TOS inputs][error %d]\n", ret);
        	goto inputs_error;
        }
        ret = bind(sockfdInputs, (struct sockaddr *)&servaddrInputs, sizeof(servaddrInputs));
    	if (ret < 0)
    	{
    		LOG(0, "[UDP][bind inputs][error %d]\n", ret);
    		goto inputs_error;
    	}

    isConnectedInputs = 0;
    return;
inputs_error:
    if (sockfdInputs >= 0) close(sockfdInputs);
    sockfdInputs = -1;
    	isConnectedInputs = 0;
}

static void groovy_udp_server_init_gmc()
{
	int ret = 0;
	int flags, beTrueAddr;
	sockfdGMC = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
    	if (sockfdGMC < 0)
    	{
    		LOG(0, "[UDP][socketGMC][error %d]\n", sockfdGMC);
    		goto gmc_error;
    	}

    	memset(&servaddrGMC, 0, sizeof(servaddrGMC));
    	servaddrGMC.sin_family = AF_INET;
    	servaddrGMC.sin_addr.s_addr = htonl(INADDR_ANY);
    	servaddrGMC.sin_port = htons(UDP_PORT_GMC);

        // Non blocking socket
    	flags = fcntl(sockfdGMC, F_GETFL, 0);
    	if (flags < 0)
    	{
      		LOG(0, "[UDP][get falg GMC][error %d]\n", flags);
      		goto gmc_error;
    	}
    	flags |= O_NONBLOCK;
    	ret = fcntl(sockfdGMC, F_SETFL, flags);
    	if (ret < 0)
    	{
       		LOG(0, "[UDP][set nonblock GMC fail][error %d]\n", ret);
       		goto gmc_error;
    	}

	beTrueAddr = 1;
	ret = setsockopt(sockfdGMC, SOL_SOCKET, SO_REUSEADDR, (void*)&beTrueAddr,sizeof(beTrueAddr));
	if (ret < 0)
	{
        	LOG(0, "[UDP][SO_REUSEADDR GMC][error %d]\n", ret);
        	goto gmc_error;
        }
        ret = setsockopt(sockfdGMC, IPPROTO_IP, IP_TOS, (char*)&beTrueAddr,sizeof(beTrueAddr));
        if (ret < 0)
	{
        	LOG(0, "[UDP][IP_TOS GMC][error %d]\n", ret);
        	goto gmc_error;
        }
        ret = bind(sockfdGMC, (struct sockaddr *)&servaddrGMC, sizeof(servaddrGMC));
    	if (ret < 0)
    	{
    		LOG(0, "[UDP][bind GMC][error %d]\n", ret);
    		goto gmc_error;
    	}
		
    isConnectedGMC = 0;
    return;
gmc_error:
    if (sockfdGMC >= 0) close(sockfdGMC);
    sockfdGMC = -1;
    	isConnectedGMC = 0;
}

static uint64_t reliable_now_ms()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

static void reliable_ack(uint8_t kind, uint32_t transfer)
{
    if (fpgaFault) return;
    uint8_t response[groovy_wire::ACK_SIZE];
    groovy_wire::ack(response, reliableSession, transfer, kind, chunkPayloadSize);
#ifdef _AF_XDP
    if (doXDPServer) {
        uint8_t packet[42 + groovy_wire::ACK_SIZE]{};
        memcpy(packet, sendbuf, 42); memcpy(packet+42, response, sizeof(response));
        iphdr* ip = (iphdr*)(packet+14); udphdr* udp = (udphdr*)(packet+34);
        udp->len = htons(8+sizeof(response)); ip->tot_len = htons(28+sizeof(response));
        update_iph_checksum(ip);
        compute_udp_checksum((unsigned short*)udp, sum_udp_checksum(ip, udp->len));
        complete_tx(xsk_socket);
        uint32_t idx;
        if (!xsk_socket->frames.available || xsk_ring_prod__reserve(&xsk_socket->tx,1,&idx)!=1) return;
        const uint64_t addr = xsk_socket->frames.take();
        memcpy(xsk_umem__get_data(xsk_socket->umem->buffer,addr),packet,sizeof(packet));
        xsk_ring_prod__tx_desc(&xsk_socket->tx,idx)->addr = addr;
        xsk_ring_prod__tx_desc(&xsk_socket->tx,idx)->len = sizeof(packet);
        xsk_ring_prod__submit(&xsk_socket->tx,1); ++xsk_socket->outstanding_tx;
        complete_tx(xsk_socket);
        return;
    }
#endif
    sendto(sockfd, response, sizeof(response), 0, (sockaddr*)&reliablePeer, sizeof(reliablePeer));
}

static bool reliable_valid_modeline(const uint8_t* p)
{
    double clock; memcpy(&clock, p + 1, sizeof(clock));
    uint16_t h[4], v[4];
    for (int i = 0; i < 4; ++i) { h[i] = groovy_wire::read16(p + 9 + 2*i); v[i] = groovy_wire::read16(p + 17 + 2*i); }
    uint64_t bytes = uint64_t(h[0]) * v[0] * (rgbMode == 1 ? 4 : rgbMode == 2 ? 2 : 3) / (p[25] == 1 ? 2 : 1);
    return p[0] == CMD_SWITCHRES && std::isfinite(clock) && clock > 0 && p[25] <= 2 && h[0] && v[0] &&
           h[0] <= h[1] && h[1] <= h[2] && h[2] <= h[3] &&
           v[0] <= v[1] && v[1] <= v[2] && v[2] <= v[3] && bytes <= groovy_wire::MAX_VIDEO;
}

static bool process_reliable_packet(const uint8_t* p, int len, const sockaddr_in& peer)
{
    using namespace groovy_wire;
    if (len == int(INIT_SIZE) && p[0] == CMD_INIT && p[5] == VERSION && read32(p + 12) == MAGIC) {
        uint16_t payload = read16(p + 6);
        uint32_t session = read32(p + 8);
        if (!session || payload < 256 || payload > MAX_DATAGRAM - HEADER || p[1] > 1 || p[2] > 3 || p[3] > 2 || p[4] > 2) return true;
        bool samePeer = peer.sin_addr.s_addr == reliablePeer.sin_addr.s_addr && peer.sin_port == reliablePeer.sin_port;
        if (reliableMode == VERSION && session == reliableSession && samePeer) {
            if (memcmp(p, reliableInit, INIT_SIZE) == 0 && isConnected) reliable_ack(INIT, 0);
            return true;
        }
        reliablePeer = peer; clientaddr = peer;
        reliableSession = session; reliableControl = 0;
        memcpy(reliableInit, p, INIT_SIZE);
        reliableVideo.reset(); reliableAudio.reset();
        if (doVerbose) initVerboseFile();
        if (!setInit(p[1], p[2], p[3], p[4], VERSION, payload)) return true;
        LOG(0, "[CMD_INIT][Wi-Fi v2][session=%u payload=%u]\n", session, payload);
        reliable_ack(INIT, 0);
        return true;
    }
    if (len < 4 || read32(p) != MAGIC) return false;
    Header h;
    if (!decode(p, size_t(len), h) || reliableMode != VERSION || h.session != reliableSession ||
        peer.sin_addr.s_addr != reliablePeer.sin_addr.s_addr || peer.sin_port != reliablePeer.sin_port) return true;
    clientaddr = reliablePeer;
    if (h.kind == CLOSE) {
        if (len != int(HEADER) || h.total || h.index || h.flags) return true;
        if (newer(h.transfer, reliableControl)) {
            reliableControl = h.transfer;
            reliableVideo.reset(h.transfer); reliableAudio.reset(h.transfer);
            if (isConnected) setClose();
            LOG(0, "[Wi-Fi v2][CLOSE]%s\n", "");
        }
        if (h.transfer == reliableControl) reliable_ack(CLOSE, h.transfer);
        return true;
    }
    if (!isConnected) return true;
    if (h.kind == MODELINE) {
        if (len != int(HEADER + 26) || h.total != 26 || h.index || h.flags || !reliable_valid_modeline(p + HEADER)) return true;
        if (newer(h.transfer, reliableControl)) {
            reliableControl = h.transfer;
            reliableVideo.reset(h.transfer); reliableAudio.reset(h.transfer);
            setSwitchres((char*)p + HEADER);
            LOG(0, "[Wi-Fi v2][MODELINE][transfer=%u]\n", h.transfer);
        }
        if (h.transfer == reliableControl) reliable_ack(MODELINE, h.transfer);
        return true;
    }
    if (!poc || !poc->PoC_bytes_len || !newer(h.transfer, reliableControl)) return true;
    if (h.kind != VIDEO && h.kind != AUDIO) return true;
    if (h.kind == VIDEO && (h.field > 2 || bool(h.flags & COMPRESSED) != bool(blitCompression) ||
        (!(h.flags & COMPRESSED) && h.total != poc->PoC_bytes_len))) return true;
    if (h.kind == AUDIO && ((h.flags & COMPRESSED) || !audioChannels || h.total % (2 * audioChannels))) return true;
    Reassembly& assembly = h.kind == VIDEO ? reliableVideo : reliableAudio;
    if (!assembly.add(h, p + HEADER, size_t(len) - HEADER, chunkPayloadSize, reliable_now_ms(),
                      h.kind == VIDEO ? MAX_VIDEO : MAX_AUDIO)) return true;
    const Header& complete = assembly.header;
    if (complete.kind == VIDEO) {
        setBlit(complete.frame, complete.field, blitCompression ? complete.total : 0, 0);
        if (fpgaFault) return true;
        // All bytes are present and unique before exposing them to the FPGA.
        memcpy(buffer + HEADER_OFFSET + poc->PoC_buffer_offset, assembly.data.data(), complete.total);
        __sync_synchronize();
        for (uint32_t offset = 0; offset < complete.total;) {
            uint16_t n = uint16_t(std::min(uint32_t(65535), complete.total - offset));
            if (blitCompression) setBlitLZ4(n); else setBlitRaw(n);
            offset += n;
        }
        groovy_FPGA_status(1);
        sendACK(complete.frame, complete.vsync);
        LOG(1, "[Wi-Fi v2][VIDEO][frame=%u bytes=%u recovered=%u dropped=%u]\n", complete.frame, complete.total, assembly.recovered, assembly.dropped);
    } else {
        setBlitAudio(uint16_t(complete.total));
        memcpy(buffer + HEADER_OFFSET + AUDIO_OFFSET, assembly.data.data(), complete.total);
        __sync_synchronize();
        setBlitRawAudio(uint16_t(complete.total));
    }
    return true;
}

static void publishLegacy()
{
    if (!isConnected || fpgaFault || !poc || !poc->PoC_bytes_len) return;
    legacyPublishing = true;
    const size_t total = legacyTransfer.data.size();
    if (legacyKind == 1) setBlit(legacyFrame, legacyField, blitCompression ? total : 0, legacyDelta);
    else setBlitAudio(uint16_t(total));
    if (fpgaFault) { legacyPublishing = false; return; }
    memcpy(buffer + HEADER_OFFSET + poc->PoC_buffer_offset, legacyTransfer.data.data(), total);
    __sync_synchronize();
    for (size_t offset=0; offset<total;) {
        const uint16_t n = uint16_t(std::min(size_t(65535), total-offset));
        if (legacyKind == 2) setBlitRawAudio(n);
        else if (blitCompression) setBlitLZ4(n);
        else setBlitRaw(n);
        offset += n;
    }
    legacyPublishing = false;
}

static bool legacyCommand(const uint8_t* p, int len)
{
    if (len <= 0) return false;
    switch (p[0]) {
    case CMD_CLOSE: case CMD_GET_VERSION: case CMD_GET_STATUS: return len == 1;
    case CMD_INIT: return (len == 4 || len == 5) && p[1] <= 1 && p[2] <= 3 && p[3] <= 2 && (len == 4 || p[4] <= 2);
    case CMD_SWITCHRES: return len == 26 && reliable_valid_modeline(p);
    case CMD_AUDIO: return len == 3;
    case CMD_BLIT_VSYNC: return len == 7 || len == 11;
    case CMD_BLIT_FIELD_VSYNC: return len == 8 || len == 9 || len == 12 || len == 13;
    default: return false;
    }
}

static inline void process_packet(char *recvbufPtr, int len)
{
    if (len > 0)
    {
        if (legacyTransfer.expire(reliable_now_ms())) isBlitting = isCorePriority = 0;
        if (legacyTransfer.active()) {
            if (legacyTransfer.accepts(size_t(len))) {
                const int result = legacyTransfer.add((const uint8_t*)recvbufPtr, size_t(len), reliable_now_ms());
                if (result == 1) publishLegacy();
                else if (result < 0) isBlitting = isCorePriority = 0;
                return;
            }
            legacyTransfer.reset(); isBlitting = isCorePriority = 0;
            // Re-synchronize only on a structurally valid command.
        }
        if (!legacyCommand((const uint8_t*)recvbufPtr, len)) return;
        if (!groovy_safe::legacySessionAllows(uint8_t(recvbufPtr[0]), isConnected && !fpgaFault,
                poc && poc->PoC_bytes_len)) return;

		if (!isBlitting)
		{
    			switch (recvbufPtr[0])
    			{
    				case CMD_GET_VERSION:
				{
					if (len == 1)
					{						
						LOG(1, "[CMD_GET_VERSION][%d][ver=%d]\n", recvbufPtr[0], GROOVY_VERSION);
						sendVersion();
					}
				}; break;
				
	    			case CMD_CLOSE:
				{
					if (len == 1)
					{
						LOG(1, "[CMD_CLOSE][%d]\n", recvbufPtr[0]);
						setClose();
					}
				}; break;

				case CMD_INIT:
				{
					if (len == 4 || len == 5)
					{
						if (doVerbose)
						{
							initVerboseFile();
						}
						uint8_t compression = recvbufPtr[1];
						uint8_t audio_rate = recvbufPtr[2];
						uint8_t audio_channels = recvbufPtr[3];
						uint8_t rgb_mode = (len >= 5) ? recvbufPtr[4] : 0;
                        LOG(1, "[CMD_INIT][legacy][LZ4=%d Audio=%d/%d RGB=%d]\n", compression, audio_rate, audio_channels, rgb_mode);
                        reliableVideo.reset(); reliableAudio.reset();
                        if (!setInit(compression, audio_rate, audio_channels, rgb_mode, 0, 0)) break;
						sendACK(0, 0);						
					}
				}; break;

				case CMD_SWITCHRES:
				{
					if (len == 26)
					{
						LOG(1, "[CMD_SWITCHRES][%d]\n", recvbufPtr[0]);
			       			setSwitchres(&recvbufPtr[0]);			       			
			       		}
				}; break;

				case CMD_AUDIO:
				{
					if (len == 3)
					{
						uint16_t udp_bytes_samples = ((uint16_t) recvbufPtr[2]  << 8) | recvbufPtr[1];
						LOG(1, "[CMD_AUDIO][%d][Bytes=%d]\n", recvbufPtr[0], udp_bytes_samples);
						setBlitAudio(udp_bytes_samples);						
					}
				}; break;

				case CMD_GET_STATUS:
				{
					if (len == 1)
					{
						groovy_FPGA_status(1);
						sendACK(0, 0);
			       			LOG(1, "[CMD_GET_STATUS][%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][AUDIO=%d][LZ4 inf=%d]\n", recvbufPtr[0], fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_audio, fpga_lz4_uncompressed);

					}
				}; break;

				case CMD_BLIT_VSYNC: //deprecated
				{
					if (len == 7 || len == 11)
					{
						uint32_t udp_lz4_size = 0;																							
						uint32_t udp_frame = ((uint32_t) recvbufPtr[4]  << 24) | ((uint32_t)recvbufPtr[3]  << 16) | ((uint32_t)recvbufPtr[2]  << 8) | recvbufPtr[1];								
						uint8_t udp_field = (poc->PoC_FB_progressive) ? 0 : 2;	
						uint16_t udp_vsync = ((uint16_t) recvbufPtr[6]  << 8) | recvbufPtr[5];						
						if (len == 11 && blitCompression)
						{
							udp_lz4_size = ((uint32_t) recvbufPtr[10]  << 24) | ((uint32_t)recvbufPtr[9]  << 16) | ((uint32_t)recvbufPtr[8]  << 8) | recvbufPtr[7];								
							LOG(1, "[CMD_BLIT][%d][Frame=%d][Vsync=%d][CSize=%d]\n", recvbufPtr[0], udp_frame, udp_vsync, udp_lz4_size);
						}
						else
						{
							LOG(1, "[CMD_BLIT][%d][Frame=%d][Vsync=%d]\n", recvbufPtr[0], udp_frame, udp_vsync);
						}																	
				       		setBlit(udp_frame, udp_field, udp_lz4_size, 0);
				       		groovy_FPGA_status(1);
				       		//LOG(1, "[GET_STATUS][DDR fr=%d bl=%d][GPU vc=%d fr=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 state_1=%d inf=%d wr=%d, run=%d resume=%d t1=%d t2=%d cmd_fskip=%d stop=%d AB=%d com=%d grav=%d lleg=%d, sub=%d blit=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_vcount, fpga_vga_frame, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_state, fpga_lz4_uncompressed, fpga_lz4_writed, fpga_lz4_run, fpga_lz4_resume, fpga_lz4_test1, fpga_lz4_test2, fpga_lz4_cmd_fskip, fpga_lz4_stop, fpga_lz4_ABCD, fpga_lz4_compressed, fpga_lz4_gravats, fpga_lz4_llegits, fpga_lz4_subframe_bytes, fpga_lz4_subframe_blit);
				       		sendACK(udp_frame, udp_vsync);	
				       		usingOldBlit = 1;			       		
				       	}
				}; break;
				
				case CMD_BLIT_FIELD_VSYNC:
				{
					if (len == 8 || len == 12 || len == 9 || len == 13)
					{
						uint32_t udp_lz4_size = 0;
						uint8_t udp_frame_delta = 0;
						uint32_t udp_frame = ((uint32_t) recvbufPtr[4]  << 24) | ((uint32_t)recvbufPtr[3]  << 16) | ((uint32_t)recvbufPtr[2]  << 8) | recvbufPtr[1];
						uint8_t udp_field = (poc->PoC_FB_progressive) ? 0 : (uint8_t) recvbufPtr[5];
						uint16_t udp_vsync = ((uint16_t) recvbufPtr[7]  << 8) | recvbufPtr[6];	
						if (len == 9 && !blitCompression)
						{
							udp_frame_delta = recvbufPtr[8]; 
						}		
						if (len == 13 && blitCompression)
						{
							udp_frame_delta = recvbufPtr[12];
						}			
						if ((len == 12 || len == 13) && blitCompression)
						{
							udp_lz4_size = ((uint32_t) recvbufPtr[11]  << 24) | ((uint32_t)recvbufPtr[10]  << 16) | ((uint32_t)recvbufPtr[9]  << 8) | recvbufPtr[8];								
							LOG(1, "[CMD_BLIT][%d][Frame=%d(%d)][Vsync=%d][CSize=%d][Delta=%d]\n", recvbufPtr[0], udp_frame, udp_field, udp_vsync, udp_lz4_size, udp_frame_delta);							
						}
						else							
						{
							LOG(1, "[CMD_BLIT][%d][Frame=%d(%d)][Vsync=%d][Dup=%d]\n", recvbufPtr[0], udp_frame, udp_field, udp_vsync, udp_frame_delta);
						}				       																
				       		setBlit(udp_frame, udp_field, udp_lz4_size, udp_frame_delta);
				       		groovy_FPGA_status(1);
				       		//LOG(1, "[GET_STATUS][DDR fr=%d bl=%d][GPU vc=%d fr=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 state_1=%d inf=%d wr=%d, run=%d resume=%d t1=%d t2=%d cmd_fskip=%d stop=%d AB=%d com=%d grav=%d lleg=%d, sub=%d blit=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_vcount, fpga_vga_frame, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_state, fpga_lz4_uncompressed, fpga_lz4_writed, fpga_lz4_run, fpga_lz4_resume, fpga_lz4_test1, fpga_lz4_test2, fpga_lz4_cmd_fskip, fpga_lz4_stop, fpga_lz4_ABCD, fpga_lz4_compressed, fpga_lz4_gravats, fpga_lz4_llegits, fpga_lz4_subframe_bytes, fpga_lz4_subframe_blit);
				       		sendACK(udp_frame, udp_vsync);				       		
				       	}
				}; break;

				default:
				{
					LOG(1,"command: %i (len=%d)\n", recvbufPtr[0], len);
				}
			}
		}
    }
}


#ifdef _AF_XDP
static inline int process_packet_eth(struct xsk_socket_info *xsk, uint64_t addr, uint32_t len)
{
    groovy_safe::Datagram datagram;
    const uint8_t* packet = (const uint8_t*)xsk_umem__get_data(xsk->umem->buffer, addr);
    if (!groovy_safe::ethernetUDP(packet, len, datagram)) return 0;
    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    memcpy(&peer.sin_addr.s_addr, packet+26, 4);
    memcpy(&peer.sin_port, packet+34, 2);

	uint8_t tmp_mac[ETH_ALEN];
	struct in_addr tmp_ip;
	struct ethhdr *eth = (struct ethhdr *) xsk_umem__get_data(xsk->umem->buffer, addr);
	struct iphdr *ip = (struct iphdr *) ((uint8_t *) eth + ETH_HLEN); //14 + 20
	struct udphdr *udp = (struct udphdr *) (((char *)ip ) + sizeof(iphdr)); //34 + 8
	int udp_len;
	char* data_pointer = (char*)eth;
	udp_len = ntohs(udp->len) - sizeof(struct udphdr);

	//set headers preparing send acks
    const bool initPacket = datagram.size >= 4 && packet[42] == CMD_INIT &&
        (datagram.size == 4 || datagram.size == 5 || datagram.size == groovy_wire::INIT_SIZE);
	if ((!isConnected || initPacket) && datagram.port == UDP_PORT)
	{
		ip->tos = 7 << 5; //max priority
		
		memset(&clientaddr, 0, sizeof (clientaddr));
 		clientaddr.sin_family = AF_INET;
		clientaddr.sin_addr.s_addr = ip->saddr;
		clientaddr.sin_port = udp->source;

		memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
		memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
		memcpy(eth->h_source, tmp_mac, ETH_ALEN);

		memcpy(&tmp_ip, &ip->saddr, sizeof(tmp_ip));
		memcpy(&ip->saddr, &ip->daddr, sizeof(tmp_ip));
		memcpy(&ip->daddr, &tmp_ip, sizeof(tmp_ip));

		memcpy(&udp->dest,&udp->source, sizeof(udp->dest));
		udp->source = htons(UDP_PORT);				
		
		//precalculate ip checksum header	
		udp->check = 0;
		ip->check = 0;	
		udp->len = htons(13 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 13);
		update_iph_checksum(ip);
		ip_check_13 = ip->check;		
		udp_check_13 = sum_udp_checksum(ip, udp->len); 
				
		udp->check = 0;
		ip->check = 0;	
		udp->len = htons(1 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 1);
		update_iph_checksum(ip);
		ip_check_1 = ip->check;		
		udp_check_1 = sum_udp_checksum(ip, udp->len); 						
				
		memcpy(&sendbuf[0], &data_pointer[0], 42);
	}

	//set headers preparing send inputs
	if (datagram.port == UDP_PORT_INPUTS && (doPs2Inputs || doJoyInputs))
	{
		ip->tos = 7 << 5; //max priority
		
		memset(&clientaddrInputs, 0, sizeof (clientaddrInputs));
 		clientaddrInputs.sin_family = AF_INET;
		clientaddrInputs.sin_addr.s_addr = ip->saddr;
		clientaddrInputs.sin_port = udp->source;

		memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
		memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
		memcpy(eth->h_source, tmp_mac, ETH_ALEN);

		memcpy(&tmp_ip, &ip->saddr, sizeof(tmp_ip));
		memcpy(&ip->saddr, &ip->daddr, sizeof(tmp_ip));
		memcpy(&ip->daddr, &tmp_ip, sizeof(tmp_ip));

		memcpy(&udp->dest,&udp->source, sizeof(udp->dest));
		udp->source = htons(UDP_PORT_INPUTS);		
		
		//precalculate ip checksum headers
		udp->check = 0;
		ip->check = 0;
		udp->len = htons(9 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 9);
		update_iph_checksum(ip);
		inputs_ip_check_9 = ip->check;
		inputs_udp_check_9 = sum_udp_checksum(ip, udp->len); 

		udp->check = 0;
		ip->check = 0;
		udp->len = htons(17 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 17);
		update_iph_checksum(ip);
		inputs_ip_check_17 = ip->check;
		inputs_udp_check_17 = sum_udp_checksum(ip, udp->len); 

		udp->check = 0;
		ip->check = 0;
		udp->len = htons(37 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 37);
		update_iph_checksum(ip);
		inputs_ip_check_37 = ip->check;
		inputs_udp_check_37 = sum_udp_checksum(ip, udp->len); 

		udp->check = 0;
		ip->check = 0;
		udp->len = htons(41 + sizeof(struct udphdr));
		ip->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + 41);
		update_iph_checksum(ip);
		inputs_ip_check_41 = ip->check;
		inputs_udp_check_41 = sum_udp_checksum(ip, udp->len); 

		memcpy(&sendbufInputs[0], &data_pointer[0], 42);
		isConnectedInputs = 1;
		
		return udp_len;
	}
	
	//set headers preparing send gmc
	if (datagram.port == UDP_PORT_GMC)
	{
		ip->tos = 7 << 5; //max priority
		
		memset(&clientaddrGMC, 0, sizeof (clientaddrGMC));
 		clientaddrGMC.sin_family = AF_INET;
		clientaddrGMC.sin_addr.s_addr = ip->saddr;
		clientaddrGMC.sin_port = udp->source;

		memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
		memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
		memcpy(eth->h_source, tmp_mac, ETH_ALEN);

		memcpy(&tmp_ip, &ip->saddr, sizeof(tmp_ip));
		memcpy(&ip->saddr, &ip->daddr, sizeof(tmp_ip));
		memcpy(&ip->daddr, &tmp_ip, sizeof(tmp_ip));

		memcpy(&udp->dest,&udp->source, sizeof(udp->dest));
		udp->source = htons(UDP_PORT_GMC);
				
		udp->check = 0;						
		memcpy(&sendbufGMC[0], &data_pointer[0], 42);
		isConnectedGMC = 1;							
		
		return udp_len;
	}

    if (datagram.port != UDP_PORT) return 0;
    const bool handled = (!isBlitting || reliableMode) &&
        process_reliable_packet(packet+datagram.offset, int(datagram.size), peer);
    if (!handled && (!reliableMode || ((datagram.size == 4 || datagram.size == 5) && packet[42] == CMD_INIT))) {
        process_packet((char*)packet+datagram.offset, int(datagram.size));
    }

	return udp_len;
}

static inline bool handle_receive_packets(struct xsk_socket_info *xsk)
{
    complete_tx(xsk);
    uint32_t idx_fq = 0, idx_rx = 0;
    const unsigned fill = std::min(unsigned(RX_BATCH_SIZE),
        std::min(xsk_prod_nb_free(&xsk->umem->fq, RX_BATCH_SIZE), xsk->frames.available));
    if (fill && xsk_ring_prod__reserve(&xsk->umem->fq, fill, &idx_fq) == fill) {
        for (unsigned i=0; i<fill; ++i)
            *xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq+i) = xsk->frames.take();
        xsk_ring_prod__submit(&xsk->umem->fq, fill);
    }
    if (xsk_ring_prod__needs_wakeup(&xsk->umem->fq)) {
        pollfd fd = {xsk_socket__fd(xsk->xsk), POLLIN, 0};
        poll(&fd, 1, 0);
    }
    const unsigned count = xsk_ring_cons__peek(&xsk->rx, RX_BATCH_SIZE, &idx_rx);
    for (unsigned i=0; i<count; ++i) {
        const xdp_desc* desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx+i);
        if (xsk->frames.validPacket(desc->addr, desc->len))
            process_packet_eth(xsk, desc->addr, desc->len);
        xsk_free_umem_frame(xsk, desc->addr);
    }
    if (count) xsk_ring_cons__release(&xsk->rx, count);
    return count == RX_BATCH_SIZE;
}
#endif

static void groovy_start()
{
	if (!groovyServer)
	{
		printf("Groovy-Server %d starting\n", GROOVY_VERSION);
        printf("%s evolution=%s\n", groovy_receiver::profile_id, groovy_receiver::evolution);
        resetStreamSession();
        fpgaFault = false;

		// get HPS Server Settings
		groovy_FPGA_hps();

		// arm clock
		if (doARMClock)
		{
			setARMClock(doARMClock);
		}

		// reset fpga
    		groovy_FPGA_init(0, 0, 0, 0);

		// map DDR
		groovy_map_ddr();

		groovyServer = 1;
	}


    	// UDP Server
    	if (!doXDPServer)
    	{
		groovy_udp_server_init();
	}
#ifdef _AF_XDP	
	else
	{
		groovy_xdp_server_init();
	}
#endif
	if (groovyServer != 2)
	{
		goto start_error;
	}

	if (!doXDPServer && (doPs2Inputs || doJoyInputs))
	{
		groovy_udp_server_init_inputs();
	}		
	
	if (!doXDPServer)
	{
		groovy_udp_server_init_gmc();
	}	
	
	user_io_status_set(SERVER_TYPE_OPT, (uint32_t)doXDPServer);
		
	// load LOGO
	if (doScreensaver)
	{
		loadLogo(1);
		groovy_FPGA_init(1, 0, 0, 0);
		groovy_FPGA_blit();
		groovy_FPGA_logo(1);
		groovyLogo = 1;
	}

    	printf("Groovy-Server %d started\n", GROOVY_VERSION);

start_error:
    	{}
}

bool groovy_stop()
{
    bool resources = groovyServer || map || sockfd >= 0 || sockfdInputs >= 0 || sockfdGMC >= 0;
#ifdef _AF_XDP
    resources = resources || xsk_socket || umem || packet_buffer || xdp_object || xdp_attached;
#endif
    if (!resources) return true; // Repeated hooks must not touch another core.
    LOG(0, "[Groovy STOP] releasing transport before core change/exec%s\n", "");
    resetStreamSession();
#ifdef _AF_XDP
    if (xsk_socket || umem || packet_buffer || xdp_object || xdp_attached) {
        if (!cleanupXdp()) {
            groovyServer = 1; // Never poll a partially released XSK.
            return false;
        }
    }
#endif
    if (sockfd >= 0) close(sockfd);
    if (sockfdInputs >= 0) close(sockfdInputs);
    if (sockfdGMC >= 0) close(sockfdGMC);
    sockfd = sockfdInputs = sockfdGMC = -1;
    if (map) shmem_unmap(map, BUFFERSIZE);
    map = buffer = nullptr;
    if (doARMClock) { setARMClock(0); doARMClock = 0; }
    groovyLogo = 0;
    groovyServer = 0;
    LOG(0, "[Groovy STOP] complete%s\n", "");
    return true;
}

void groovy_poll()
{
    unsigned pollBudget = groovy_receiver::poll_batches;
    bool morePackets = false;
    if (legacyTransfer.expire(reliable_now_ms())) isBlitting = isCorePriority = 0;
    reliableVideo.expire(reliable_now_ms()); reliableAudio.expire(reliable_now_ms());
	if (groovyServer != 2)
	{
		groovy_start();
		return;
	}

	do
	{
		if (doVerbose == 3 && isConnected && poc->PoC_bytes_len > 0 && CheckTimer(statusLogTime))
		{
            statusLogTime = GetTimer(10); // 100 samples/s, not one log per poll/packet.
			groovy_FPGA_status(0);
			//LOG(3, "[GET_STATUS][DDR fr=%d bl=%d][GPU vc=%d fr=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 state_1=%d inf=%d wr=%d, run=%d resume=%d t1=%d t2=%d cmd_fskip=%d stop=%d AB=%d com=%d grav=%d lleg=%d, sub=%d blit=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_vcount, fpga_vga_frame, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_state, fpga_lz4_uncompressed, fpga_lz4_writed, fpga_lz4_run, fpga_lz4_resume, fpga_lz4_test1, fpga_lz4_test2, fpga_lz4_cmd_fskip, fpga_lz4_stop, fpga_lz4_ABCD, fpga_lz4_compressed, fpga_lz4_gravats, fpga_lz4_llegits, fpga_lz4_subframe_bytes, fpga_lz4_subframe_blit);
			LOG(3, "[GET_STATUS][DDR fr=%d bl=%d][GPU fr=%d vc=%d fskip=%d vb=%d fd=%d][VRAM px=%d queue=%d sync=%d free=%d eof=%d][LZ4 un=%d]\n", poc->PoC_frame_ddr, numBlit, fpga_vga_frame, fpga_vga_vcount, fpga_vga_frameskip, fpga_vga_vblank, fpga_vga_f1, fpga_vram_pixels, fpga_vram_queue, fpga_vram_synced, fpga_vram_ready, fpga_vram_end_frame, fpga_lz4_uncompressed);
		}

		if (!doXDPServer)
		{
            // Match XDP's bounded packet budget, not just four UDP datagrams.
            morePackets = false;
            for (unsigned packet = 0; packet < groovy_receiver::receive_batch; ++packet) {
                sockaddr_in peer = {};
                socklen_t peerLen = sizeof(peer);
                char* target = recvbuf;
                int len = recvfrom(sockfd, target, 65536, 0, (sockaddr*)&peer, &peerLen);
                if (len <= 0) break;
                morePackets = packet + 1 == groovy_receiver::receive_batch;
                bool handled = (!isBlitting || reliableMode) && process_reliable_packet((uint8_t*)target, len, peer);
                if (!handled && (!reliableMode || ((len == 4 || len == 5) && target[0] == CMD_INIT))) {
                    clientaddr = peer; clilen = peerLen;
                    process_packet(target, len);
                }
            }

		}
#ifdef _AF_XDP
		else
		{						
			morePackets = handle_receive_packets(xsk_socket);
		}
#endif		
	} while ((isCorePriority || morePackets) && --pollBudget);

	if (doScreensaver && groovyLogo)
	{
		loadLogo(0);
	}

	if (fp && doVerbose > 0 && CheckTimer(logTime))
   	{
        fflush(fp);
        if (ftell(fp) >= LOG_MAX_BYTES) {
            fclose(fp); fp = NULL;
            // Retain one previous bounded log. Opening the current file truncates it
            // even if rename fails (e.g. an older run filled /tmp).
            rename("/tmp/groovy.log", "/tmp/groovy.log.1");
            fp = fopen("/tmp/groovy.log", "wt");
        }
		logTime = GetTimer(LOG_TIMER);
   	}

}

void groovy_send_joystick(unsigned char joystick, uint32_t map)
{
	poc->PoC_joystick_order++;
	if (joystick == 0)
	{
		poc->PoC_joystick_map1 = map;
	}
	if (joystick == 1)
	{
		poc->PoC_joystick_map2 = map;
	}

	if (isConnectedInputs && doJoyInputs)
	{
		groovy_send_joysticks();
		LOG(2, "[JOY_ACK][%d][map=%d]\n", joystick, map);
	}
	else
	{
		LOG(2, "[JOY][%d][map=%d]\n", joystick, map);
	}
}

void groovy_send_analog(unsigned char joystick, unsigned char analog, char valueX, char valueY)
{
	poc->PoC_joystick_order++;
	if (joystick == 0)
	{
		if (analog == 0)
		{
			poc->PoC_joystick_l_analog_X1 = valueX;
			poc->PoC_joystick_l_analog_Y1 = valueY;
		}
		else
		{
			poc->PoC_joystick_r_analog_X1 = valueX;
			poc->PoC_joystick_r_analog_Y1 = valueY;
		}
	}
	if (joystick == 1)
	{
		if (analog == 0)
		{
			poc->PoC_joystick_l_analog_X2 = valueX;
			poc->PoC_joystick_l_analog_Y2 = valueY;
		}
		else
		{
			poc->PoC_joystick_r_analog_X2 = valueX;
			poc->PoC_joystick_r_analog_Y2 = valueY;
		}
	}

	if (isConnectedInputs && doJoyInputs == 2)
	{
		groovy_send_joysticks();
		LOG(2, "[JOY_%s_ACK][%d][x=%d,y=%d]\n", (analog) ? "R" : "L", joystick, valueX, valueY);
	}
	else
	{
		LOG(2, "[JOY_%s][%d][x=%d,y=%d]\n", (analog) ? "R" : "L", joystick, valueX, valueY);
	}
}

void groovy_send_keyboard(uint16_t key, int press)
{
	poc->PoC_ps2_order++;
	int index = key2sdl[key];
	int bit = 1 & (poc->PoC_ps2_keyboard_keys[index / 8] >> (index % 8));
	if (bit)
	{
		if (!press)
		{
			poc->PoC_ps2_keyboard_keys[index / 8] ^= 1 << (index % 8);
		}
	}
	else
	{
		if (press)
		{
			poc->PoC_ps2_keyboard_keys[index / 8] ^= 1 << (index % 8);
		}
	}

	if (isConnectedInputs && doPs2Inputs)
	{
		groovy_send_ps2();
		LOG(2, "[KBD_ACK][key=%d sdl=%d (%d->%d)]\n", key, index, bit, press);
	}
	else
	{
		LOG(2, "[KBD][key=%d sdl=%d (%d->%d)]\n", key, index, bit, press);
	}
}

void groovy_send_mouse(unsigned char ps2, unsigned char x, unsigned char y, unsigned char z)
{
	bitByte bits;
	bits.byte = ps2;
	poc->PoC_ps2_order++;
	poc->PoC_ps2_mouse = ps2;
	poc->PoC_ps2_mouse_x = x;
	poc->PoC_ps2_mouse_y = y;
	poc->PoC_ps2_mouse_z = z;
	if (isConnectedInputs && doPs2Inputs == 2)
	{
		groovy_send_ps2();
		LOG(2, "[MIC_ACK][yo=%d,xo=%d,ys=%d,xs=%d,1=%d,bm=%d,br=%d,bl=%d][x=%d,y=%d,z=%d]\n", bits.u.bit7, bits.u.bit6, bits.u.bit5, bits.u.bit4, bits.u.bit3, bits.u.bit2, bits.u.bit1, bits.u.bit0, x , y , z);
	}
	else
	{
		LOG(2, "[MIC][yo=%d,xo=%d,ys=%d,xs=%d,1=%d,bm=%d,br=%d,bl=%d][x=%d,y=%d,z=%d]\n", bits.u.bit7, bits.u.bit6, bits.u.bit5, bits.u.bit4, bits.u.bit3, bits.u.bit2, bits.u.bit1, bits.u.bit0, x , y , z);
	}
}

void groovy_user_io_file_gmc(const char* name)
{		
#ifndef _AF_XDP		
	if (!isConnectedGMC)
	{					
		int len = recvfrom(sockfdGMC, recvbuf, 1, 0, (struct sockaddr *)&clientaddrGMC, &clilen);
		if (len > 0)
		{
			char hoststr[NI_MAXHOST];
			char portstr[NI_MAXSERV];			
			getnameinfo((struct sockaddr *)&clientaddrGMC, clilen, hoststr, sizeof(hoststr), portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
			LOG(1,"[GMC][%s:%s]\n", hoststr, portstr);  			
			isConnectedGMC = 1;
		}					
	}
#endif	
	LOG(0,"[GMC][%s]\n", name); 
	size_t fSize = 0;
	char* sendbufPtr = (doXDPServer) ? (char*) &sendbufGMC[42] : (char*) &sendbufGMC[0];
	fSize = FileLoad(name, sendbufPtr, 65536);	

    	if (isConnectedGMC)
    	{
    		LOG(2, "[GMC][Send]%s\n", sendbufPtr);
    		if (!doXDPServer)
    		{
    			sendto(sockfdGMC, sendbufPtr, fSize, 0, (struct sockaddr *)&clientaddrGMC, clilen);
    		}	
#ifdef _AF_XDP
		else
		{
			//struct ethhdr *eth = (struct ethhdr *)(sendbufInputs);
			struct iphdr *iph = (struct iphdr *)(sendbufGMC + sizeof(struct ethhdr));
			struct udphdr *udph = (struct udphdr *)(sendbufGMC + sizeof(struct ethhdr) + (iph->ihl * 4));
			int ret = 0;
			uint32_t tx_idx = 0;
			uint64_t addr = 0;
			complete_tx(xsk_socket);
		if (!xsk_socket->frames.available) return;
		ret = xsk_ring_prod__reserve(&xsk_socket->tx, 1, &tx_idx);
			if (ret != 1) {
				// No more transmit slots, drop the packet
				LOG(0, "[ACK_%s][Failed]\n", "STATUS");
				return;
			}	
			iph->tot_len = htons(sizeof(iphdr) + sizeof(struct udphdr) + fSize);
			update_iph_checksum(iph);
			udph->check = 0;
			udph->len = htons(fSize + sizeof(struct udphdr));
			uint32_t udph_sum = sum_udp_checksum(iph, udph->len); 						
			compute_udp_checksum((unsigned short *)udph, udph_sum);																				
			addr = xsk_socket->frames.take();
			memcpy(xsk_umem__get_data(xsk_socket->umem->buffer, addr), sendbufGMC, 42 + fSize);
			xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->addr = addr;
			xsk_ring_prod__tx_desc(&xsk_socket->tx, tx_idx)->len = 42 + fSize;
			xsk_ring_prod__submit(&xsk_socket->tx, 1);
			xsk_socket->outstanding_tx++;
	
			complete_tx(xsk_socket);			
		}
#endif    		
    	}
    	else
    	{
    		LOG(2, "[GMC][Read]%s\n", sendbufPtr);
    	}
    	
}




     
