#include "groovymister.h"

#include <stdint.h>
#include <stdio.h>
#include <cmath>
#include <errno.h>
#include <cstring>
#include <algorithm>
#include <limits>
#include <chrono>
#include <atomic>
#include <vector>
#include <new>
#include "../protocol/groovy_protocol.h"

#ifndef _WIN32
 #include <netinet/udp.h>
 #include <sys/types.h>
 #include <sys/socket.h>
 #include <arpa/inet.h>
 #include <netinet/in.h>
 #include <fcntl.h>
 #include <sys/time.h>
 #include <sys/stat.h>
 #include <time.h>
 #include <unistd.h>
#endif

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"



#define LOG(sev,fmt, ...) do {\
					if (sev <= m_verbose) {\
					printf(fmt, __VA_ARGS__);\
								}\
							} while (0)

#define CMD_CLOSE 1
#define CMD_INIT 2
#define CMD_SWITCHRES 3
#define CMD_AUDIO 4
#define CMD_GET_STATUS 5
#define CMD_BLIT_VSYNC 6
#define CMD_BLIT_FIELD_VSYNC 7
#define CMD_GET_VERSION 8

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

#define LZ4_ADAPTATIVE_CSIZE 600000
#define K_CONGESTION_SIZE    500000
#define K_CONGESTION_TIME    110000

GroovyMister::GroovyMister()
{
	memset(this, 0, sizeof(*this));
#ifdef _WIN32
	m_sockFD = m_sockInputsFD = INVALID_SOCKET;
	QueryPerformanceFrequency(&m_tickFrequency);
#else
	m_sockFD = m_sockInputsFD = -1;
#endif
	m_ackKind = 255;
	setTimeStart(); m_tickEnd = m_tickSync = m_tickCongestion = m_tickStart;
	m_verbose = 0;
	m_lz4Frames = 0;
	m_soundChan = 0;
	m_rgbMode = 0;

	fpga.frame = 0;
	fpga.frameEcho = 0;
	fpga.vCount = 0;
	fpga.vCountEcho = 0;
	fpga.vramEndFrame = 0;
	fpga.vramReady = 0;
	fpga.vramSynced = 0;
	fpga.vgaFrameskip = 0;
	fpga.vgaVblank = 0;
	fpga.vgaF1 = 0;
	fpga.audio = 0;
	fpga.vramQueue = 0;

	joyInputs.joyFrame = 0;
	joyInputs.joyOrder = 0;
	joyInputs.joy1 = 0;
	joyInputs.joy2 = 0;
	joyInputs.joy1LXAnalog = 0;
	joyInputs.joy1LYAnalog = 0;
	joyInputs.joy1RXAnalog = 0;
	joyInputs.joy1RYAnalog = 0;
	joyInputs.joy2LXAnalog = 0;
	joyInputs.joy2LYAnalog = 0;
	joyInputs.joy2RXAnalog = 0;
	joyInputs.joy2RYAnalog = 0;

	ps2Inputs.ps2Frame = 0;
	ps2Inputs.ps2Order = 0;
	memset(&ps2Inputs.ps2Keys, 0, sizeof(ps2Inputs.ps2Keys));
	ps2Inputs.ps2Mouse = 0;
	ps2Inputs.ps2MouseX = 0;
	ps2Inputs.ps2MouseY = 0;
	ps2Inputs.ps2MouseZ = 0;

	m_RGBSize = 0;
	m_interlace = 0;
	m_vTotal = 0;
	m_frame = 0;
	m_frameTime = 0;
	m_streamTime = 0;
	m_emulationTime = 0;
	m_mtu = 0;
	m_doCongestionControl = 0;
	m_network_ping = 0;
	m_delta_enabled[0] = 0;
	m_delta_enabled[1] = 0;
	m_isConnected = 0;
	m_reliableMode = 0;
	DWORD totalBufferCount = 0;
	DWORD totalBufferSize = 0;
	m_pBufferAudio = AllocateBufferSpace(BUFFER_SIZE, 1, totalBufferSize, totalBufferCount);
	m_pBufferBlitDelta = AllocateBufferSpace(BUFFER_SIZE, 1, totalBufferSize, totalBufferCount);
	for(int i=0;i<2;i++)
	{
		m_pBufferBlit[i] = AllocateBufferSpace(BUFFER_SIZE, 1, totalBufferSize, totalBufferCount);
		m_pBufferLZ4[i] = AllocateBufferSpace(LZ4_compressBound(BUFFER_SIZE), 1, totalBufferSize, totalBufferCount);
	}

}


GroovyMister::~GroovyMister()
{
	CmdClose();
#ifdef _WIN32
	VirtualFree(m_pBufferAudio, 0, MEM_RELEASE);
	VirtualFree(m_pBufferBlitDelta, 0, MEM_RELEASE);
	for(int i=0;i<2;i++)
	{
		VirtualFree(m_pBufferBlit[i], 0, MEM_RELEASE);
		VirtualFree(m_pBufferLZ4[i], 0, MEM_RELEASE);
	}
#else
	free(m_pBufferAudio);
	free(m_pBufferBlitDelta);
	for(int i=0;i<2;i++)
	{
		free(m_pBufferBlit[i]);
		free(m_pBufferLZ4[i]);
	}
#endif
}

char* GroovyMister::getPBufferBlit(uint8_t field)
{
	return (field < 2 && m_RGBSize) ? m_pBufferBlit[field] : NULL;
}

char* GroovyMister::getPBufferBlitDelta(void)
{
	return m_pBufferBlitDelta;
}

char* GroovyMister::getPBufferAudio(void)
{
	return m_pBufferAudio;
}

GroovyMister* GroovyMister::Create() { return new GroovyMister; }
void GroovyMister::Destroy(GroovyMister* instance) { delete instance; }

void GroovyMister::CmdClose(void)
{
    if (m_isConnected) {
        if (m_reliableMode) {
            uint32_t id = ++m_transfer;
            for (int i = 0; i < 2; ++i) {
                SendReliable(groovy_wire::CLOSE, "", 0, 0, 0, 0, 0, id);
                if (WaitControl(groovy_wire::CLOSE, id, 60)) break;
            }
        } else { const char closeCommand = CMD_CLOSE; Send(&closeCommand, 1); }
    }
    m_isConnected = 0;
#ifdef _WIN32
    if (m_sockFD != INVALID_SOCKET) closesocket(m_sockFD);
    if (m_sockInputsFD != INVALID_SOCKET) closesocket(m_sockInputsFD);
    m_sockFD = m_sockInputsFD = INVALID_SOCKET;
    if (m_wsaStarted) WSACleanup();
    m_wsaStarted = false;
#else
    if (m_sockFD >= 0) close(m_sockFD);
    if (m_sockInputsFD >= 0) close(m_sockInputsFD);
    m_sockFD = m_sockInputsFD = -1;
#endif
    m_reliableMode = 0;
}

void GroovyMister::setVerbose(uint8_t sev)
{
	m_verbose = sev;
}

const char* GroovyMister::getVersion()
{
	return &GROOVYMISTER_VERSION[0];
}

int GroovyMister::CmdInit(const char* misterHost, uint16_t misterPort, int lz4Frames, uint32_t soundRate, uint8_t soundChan, uint8_t rgbMode, uint16_t mtu)
{
    CmdClose();
    if (!misterHost || (mtu && (mtu < 1500 || mtu > 9000)) || rgbMode > 2 || soundChan > 2 ||
        lz4Frames < 0 || lz4Frames > 6 ||
        !m_pBufferAudio || !m_pBufferBlitDelta || !m_pBufferBlit[0] || !m_pBufferBlit[1] ||
        !m_pBufferLZ4[0] || !m_pBufferLZ4[1]) return -1;
    memset(&fpga, 0, sizeof(fpga));
    m_RGBSize = 0; m_frame = 0; m_frameTime = 0; m_network_ping = 0;
    m_streamTime = m_emulationTime = 0; m_doCongestionControl = 0;
    m_delta_enabled[0] = m_delta_enabled[1] = 0;
    m_ackKind = 255; m_ackTransfer = 0; m_legacyAck = false;
    m_mtu = (mtu ? mtu : 1500) - MTU_HEADER;
    m_payload = m_mtu - groovy_wire::HEADER;
    static std::atomic<uint32_t> sequence(0);
    m_session = uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) ^ ++sequence;
    if (!m_session) m_session = 1;
    m_transfer = 0;
#ifdef _WIN32
    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd)) return -1;
    m_wsaStarted = true;
#endif
    m_sockFD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (m_sockFD == INVALID_SOCKET) { CmdClose(); return -1; }
    DWORD timeout = 100;
#else
    if (m_sockFD < 0) return -1;
    timeval timeout = {0, 100000};
#endif
    setsockopt(m_sockFD, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout));
    int bufferSize = 2 * 1024 * 1024;
    setsockopt(m_sockFD, SOL_SOCKET, SO_SNDBUF, (const char*)&bufferSize, sizeof(bufferSize));
    setsockopt(m_sockFD, SOL_SOCKET, SO_RCVBUF, (const char*)&bufferSize, sizeof(bufferSize));
    memset(&m_serverAddr, 0, sizeof(m_serverAddr));
    m_serverAddr.sin_family = AF_INET;
    m_serverAddr.sin_port = htons(misterPort);
    if (inet_pton(AF_INET, misterHost, &m_serverAddr.sin_addr) != 1 ||
        connect(m_sockFD, (sockaddr*)&m_serverAddr, sizeof(m_serverAddr)) != 0) { CmdClose(); return -1; }
    m_lz4Frames = uint8_t(lz4Frames); m_soundChan = soundChan; m_rgbMode = rgbMode;
    memset(m_bufferSend, 0, sizeof(m_bufferSend));
    m_bufferSend[0] = CMD_INIT; m_bufferSend[1] = lz4Frames ? 1 : 0;
    m_bufferSend[2] = soundRate == 22050 ? 1 : soundRate == 44100 ? 2 : soundRate == 48000 ? 3 : 0;
    m_bufferSend[3] = soundChan; m_bufferSend[4] = rgbMode;
    m_bufferSend[5] = groovy_wire::VERSION;
    groovy_wire::write16(m_bufferSend + 6, m_payload);
    groovy_wire::write32(m_bufferSend + 8, m_session);
    groovy_wire::write32(m_bufferSend + 12, groovy_wire::MAGIC);
    // Only the explicit version/session ACK can enable v2. Legacy ACKs cannot enable it.
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (Send(m_bufferSend, groovy_wire::INIT_SIZE) && WaitControl(groovy_wire::INIT, 0, 120)) {
            m_reliableMode = 2; break;
        }
    }
    if (m_reliableMode) {
        LOG(0, "[MiSTer] Wi-Fi protocol v2 acknowledged (payload=%u)\n", m_payload);
    } else {
        // Preserve BOTH the 5-byte INIT and the original full-size legacy payload.
        m_ackKind = 255; m_legacyAck = false;
        for (int attempt = 0; attempt < 3 && !m_legacyAck; ++attempt) {
            Send(m_bufferSend, 5); getACK(120);
        }
        if (!m_legacyAck) { LOG(0, "[MiSTer] INIT timed out at %s\n", misterHost); CmdClose(); return -1; }
        LOG(0, "[MiSTer] Legacy protocol (payload=%u)\n", m_mtu);
    }
    m_isConnected = 1;
    setTimeStart(); m_tickEnd = m_tickSync = m_tickCongestion = m_tickStart;
    return 0;
}

void GroovyMister::CmdSwitchres(double pClock, uint16_t hActive, uint16_t hBegin, uint16_t hEnd, uint16_t hTotal, uint16_t vActive, uint16_t vBegin, uint16_t vEnd, uint16_t vTotal, uint8_t interlace)
{
	if (!m_isConnected)
	  return;
	  
    const uint64_t bytes = uint64_t(hActive) * vActive * (m_rgbMode == 1 ? 4 : m_rgbMode == 2 ? 2 : 3) / (interlace == 1 ? 2 : 1);
    if (!std::isfinite(pClock) || pClock <= 0 || interlace > 2 || !hActive || !vActive ||
        hActive > hBegin || hBegin > hEnd || hEnd > hTotal ||
        vActive > vBegin || vBegin > vEnd || vEnd > vTotal || bytes > BUFFER_SIZE) {
        LOG(0, "[MiSTer] Invalid or oversized modeline (%u x %u)\n", hActive, vActive);
        m_RGBSize = 0; return;
    }
	uint8_t interlace_modeline = (interlace != 2) ? interlace : 1;

	m_RGBSize = (m_rgbMode == 1) ? (hActive * vActive) << 2 : (m_rgbMode == 2) ? (hActive * vActive) << 1 : hActive * vActive * 3;

	if (interlace == 1)
	{
		m_RGBSize = m_RGBSize >> 1;
	}

    // Clock units are 100 ns. Round the whole field independently, not each
    // raster line to whole microseconds (which compounds error at low clocks).
    const double lineTicks = 10.0 * hTotal / pClock;
    const double fieldTicks = lineTicks * vTotal / (interlace_modeline ? 2.0 : 1.0);
    if (!std::isfinite(fieldTicks) || lineTicks < 1 || fieldTicks < 1 || fieldTicks > INT32_MAX) {
        m_RGBSize = 0; return;
    }
    m_widthTime = uint32_t(std::llround(lineTicks));
    m_frameTime = uint32_t(std::llround(fieldTicks));
	
	m_interlace = interlace_modeline;
	m_vTotal    = vTotal;
	m_delta_enabled[0] = 0;
	m_delta_enabled[1] = 0;

	m_bufferSend[0] = CMD_SWITCHRES;
	memcpy(&m_bufferSend[1],&pClock,sizeof(pClock));
	memcpy(&m_bufferSend[9],&hActive,sizeof(hActive));
	memcpy(&m_bufferSend[11],&hBegin,sizeof(hBegin));
	memcpy(&m_bufferSend[13],&hEnd,sizeof(hEnd));
	memcpy(&m_bufferSend[15],&hTotal,sizeof(hTotal));
	memcpy(&m_bufferSend[17],&vActive,sizeof(vActive));
	memcpy(&m_bufferSend[19],&vBegin,sizeof(vBegin));
	memcpy(&m_bufferSend[21],&vEnd,sizeof(vEnd));
	memcpy(&m_bufferSend[23],&vTotal,sizeof(vTotal));
	memcpy(&m_bufferSend[25],&interlace,sizeof(interlace));

    if (m_reliableMode) {
        uint32_t id = ++m_transfer;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (SendReliable(groovy_wire::MODELINE, m_bufferSend, 26, 0, 0, 0, 0, id) &&
                WaitControl(groovy_wire::MODELINE, id, 150)) return;
        }
        LOG(0, "[MiSTer] Modeline ACK timed out%s\n", "");
        m_RGBSize = 0;
    } else Send(m_bufferSend, 26);
}

void GroovyMister::CmdBlit(uint32_t frame, uint8_t field, uint16_t vCountSync, uint32_t margin, uint32_t matchDeltaBytes)
{
	if (!m_isConnected || !m_RGBSize || field > 1)
	  return;
	  
	m_frame = frame;
	uint16_t vSync = vCountSync;

	if (!vSync)
	{
		if (m_frame <= 10)
		{
			vSync = m_vTotal >> 1;
		}
		else
		{
			int64_t timeCalc = int64_t(m_network_ping) + margin + m_emulationTime - m_streamTime;
            timeCalc = std::max(int64_t(0), std::min(int64_t(m_frameTime), timeCalc));
            vSync = uint16_t(std::max(int64_t(1), int64_t(m_vTotal) - int64_t(m_vTotal) * timeCalc / m_frameTime));
		}
	}

    if (m_reliableMode) {
        const char* pixels = m_pBufferBlit[field];
        uint32_t size = m_RGBSize;
        if (m_lz4Frames) {
            int compressed = LZ4_compress_default(pixels, m_pBufferLZ4[0], m_RGBSize, LZ4_compressBound(BUFFER_SIZE));
            if (compressed <= 0 || uint32_t(compressed) > groovy_wire::MAX_VIDEO) {
                LOG(0, "[MiSTer] Frame exceeds FPGA compressed buffer: %d\n", compressed); return;
            }
            pixels = m_pBufferLZ4[0]; size = uint32_t(compressed);
        }
        setTimeStart();
        SendReliable(groovy_wire::VIDEO, pixels, size, frame, field, vSync,
                     m_lz4Frames ? groovy_wire::COMPRESSED : 0, ++m_transfer);
        setTimeEnd(); m_streamTime = DiffTime();
        return;
    }
	uint32_t cSize = 0;
	uint32_t cSizeDelta = 0;
	uint32_t bytesToSend = 0;
	double ratio_delta = 1.0;
	if (m_lz4Frames)
	{
		double ratio_match = (double) matchDeltaBytes / m_RGBSize;
		if (!(m_lz4Frames % 2 == 0) || ratio_match < 1 || !m_delta_enabled[field]) // duplicated frame, compress only delta
		{
			switch (m_lz4Frames)
			{
				case(6):
				case(5):
				case(2):
				case(1): cSize = LZ4_compress_default((char *)&m_pBufferBlit[field][0], m_pBufferLZ4[0], m_RGBSize, m_RGBSize);
						 break;
				case(4):
				case(3): cSize = LZ4_compress_HC((char *)&m_pBufferBlit[field][0], m_pBufferLZ4[0], m_RGBSize, m_RGBSize, LZ4HC_CLEVEL_DEFAULT);
						 break;
			}
		}
		else
		{
			cSize = m_RGBSize;
		}
		cSizeDelta = cSize;
		double ratio_lz4 = (double) cSize / m_RGBSize;
		if ((m_lz4Frames % 2 == 0) && m_delta_enabled[field] && ratio_lz4 > 0.05 && ratio_match > 0.20 && ratio_match > 0.9 - ratio_lz4) // try_delta size
		{
			switch (m_lz4Frames)
			{
				case(6):
				case(5):
				case(2):
				case(1): cSizeDelta = LZ4_compress_default((char *)&m_pBufferBlitDelta[0], m_pBufferLZ4[1], m_RGBSize, m_RGBSize);
						 break;
				case(4): 
				case(3): cSizeDelta = LZ4_compress_HC((char *)&m_pBufferBlitDelta[0], m_pBufferLZ4[1], m_RGBSize, m_RGBSize, LZ4HC_CLEVEL_DEFAULT);
						 break;
			}
			ratio_delta = (double) cSizeDelta / cSize;
			//LOG(0,"frame %d raw %d, match %d, ratio %f csize %d cSizeDelta %d ratio_delta %f\n",frame, m_RGBSize, matchDeltaBytes, ratio_match, cSize, cSizeDelta, ratio_delta);	
		}

		if ((m_lz4Frames == 5 || m_lz4Frames == 6) && cSizeDelta > LZ4_ADAPTATIVE_CSIZE)
		{
			if (cSize <= cSizeDelta || m_lz4Frames == 5)
			{
				cSize = LZ4_compress_HC((char *)&m_pBufferBlit[field][0], m_pBufferLZ4[0], m_RGBSize, m_RGBSize, LZ4HC_CLEVEL_DEFAULT);
			}
			else
			{
				cSizeDelta = LZ4_compress_HC((char *)&m_pBufferBlitDelta[0], m_pBufferLZ4[1], m_RGBSize, m_RGBSize, LZ4HC_CLEVEL_DEFAULT); 
				ratio_delta = (double) cSizeDelta / cSize;
			}
			m_lz4Frames = m_lz4Frames - 2;
			LOG(0,"[MiSTer] LZ4 Adaptative apply LZ4HC on frame %d\n", frame);
		}
	}

    if (m_lz4Frames && !cSize) {
        LOG(0, "[MiSTer] LZ4 compression failed, dropping frame %u\n", frame); return;
    }
	m_bufferSend[0] = CMD_BLIT_FIELD_VSYNC;
	memcpy(&m_bufferSend[1], &frame, sizeof(frame));
	memcpy(&m_bufferSend[5], &field, sizeof(field));
	memcpy(&m_bufferSend[6], &vSync, sizeof(vSync));
	if (cSize > 0)
	{
		if (ratio_delta < 0.95)
		{
			memcpy(&m_bufferSend[8], &cSizeDelta, sizeof(cSizeDelta));
			m_bufferSend[12] = 0x01; //frame_delta
			bytesToSend = cSizeDelta;
			Send(&m_bufferSend[0], 13);
		}
		else
		{
			memcpy(&m_bufferSend[8], &cSize, sizeof(cSize));
			bytesToSend = cSize;
			Send(&m_bufferSend[0], 12);
		}
	}
	else
	{
		if (m_delta_enabled[field] && matchDeltaBytes == m_RGBSize)
		{
			m_bufferSend[8] = 0x01; //frame_dup
			Send(&m_bufferSend[0], 9);
			return;
		}
		else
		{
			bytesToSend = m_RGBSize;
			Send(&m_bufferSend[0], 8);
		}
	}
	
	if (m_doCongestionControl)
	{
		m_tickStart = m_tickCongestion;
		setTimeEnd();
		m_streamTime = DiffTime();
		while (m_streamTime < K_CONGESTION_TIME)
		{
			setTimeEnd();
			m_streamTime = DiffTime();
		}
	}
	
	setTimeStart();
	uint8_t buffer_blit = (cSize > 0) ? (ratio_delta < 0.95) ? 1 : 0 : field;
	SendStream(0, buffer_blit, bytesToSend, (ratio_delta < 0.95) ? cSizeDelta : cSize);
	setTimeEnd();
	m_streamTime = DiffTime();
	m_tickCongestion = m_tickEnd;
	m_doCongestionControl = (bytesToSend >= K_CONGESTION_SIZE) ? 1 : 0;
	m_delta_enabled[field] = 1;
	//printf("[DEBUG] Stream time , frame %d -> %lu\n",m_frame, m_streamTime);
}

void GroovyMister::CmdAudio(uint16_t soundSize)
{
    if (!fpga.audio || !m_isConnected || !soundSize || soundSize > groovy_wire::MAX_AUDIO ||
        !m_soundChan || soundSize % (2 * m_soundChan)) return;
    if (m_reliableMode) {
        SendReliable(groovy_wire::AUDIO, m_pBufferAudio, soundSize, 0, 0, 0, 0, ++m_transfer);
    } else {
        m_bufferSend[0] = CMD_AUDIO;
        groovy_wire::write16(m_bufferSend + 1, soundSize);
        if (Send(m_bufferSend, 3)) SendStream(1, 0, soundSize, 0);
    }
}

uint32_t GroovyMister::getACK(DWORD dwMilliseconds)
{
#ifdef _WIN32
    if (m_sockFD == INVALID_SOCKET) return 0;
#else
    if (m_sockFD < 0) return 0;
#endif
    using namespace std::chrono;
    const auto start = steady_clock::now();
    const auto deadline = start + milliseconds(dwMilliseconds);
    uint32_t result = 0;
    for (unsigned packets = 0; packets < 256; ++packets) {
        auto now = steady_clock::now();
        int64_t waitUs = (!result && dwMilliseconds && now < deadline) ? duration_cast<microseconds>(deadline - now).count() : 0;
        timeval timeout = {long(waitUs / 1000000), long(waitUs % 1000000)};
        fd_set ready; FD_ZERO(&ready); FD_SET(m_sockFD, &ready);
        if (select(int(m_sockFD + 1), &ready, NULL, NULL, &timeout) <= 0) break;
        int len = recv(m_sockFD, m_bufferReceive, sizeof(m_bufferReceive), 0);
        if (len <= 0) break;
        bool valid = false;
        if (len == 13) {
            m_legacyAck = true;
            uint32_t frame = groovy_wire::read32(m_bufferReceive);
            if (!frame || groovy_wire::newer(frame, fpga.frameEcho)) setFpgaStatus();
            valid = true;
        } else if (len == int(groovy_wire::ACK_SIZE) && groovy_wire::read32(m_bufferReceive) == groovy_wire::MAGIC &&
                   groovy_wire::read32(m_bufferReceive + 4) == m_session && uint8_t(m_bufferReceive[13]) == groovy_wire::VERSION &&
                   groovy_wire::read16(m_bufferReceive + 14) == m_payload) {
            m_ackKind = uint8_t(m_bufferReceive[12]); m_ackTransfer = groovy_wire::read32(m_bufferReceive + 8); valid = true;
        } else if (len == 1) { m_core_version = uint8_t(m_bufferReceive[0]); }
        if (valid) result = uint32_t(std::max(int64_t(1), duration_cast<nanoseconds>(steady_clock::now() - start).count() / 100));
        if (!result && steady_clock::now() >= deadline) break;
    }
    return result;
}

bool GroovyMister::WaitControl(uint8_t kind, uint32_t transfer, uint32_t timeoutMs)
{
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    do {
        if (m_ackKind == kind && m_ackTransfer == transfer) return true;
        auto left = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
        if (left <= 0) break;
        getACK(DWORD(left));
    } while (steady_clock::now() < deadline);
    return m_ackKind == kind && m_ackTransfer == transfer;
}

void GroovyMister::WaitSync(void)
{
	if (!m_isConnected || !m_frameTime)
	  return;
	  
	m_tickStart = m_tickSync;
	setTimeEnd();
	m_emulationTime = DiffTime();
	int sleepTime = (m_emulationTime >= m_frameTime) ? 0 : m_frameTime - m_emulationTime;
	int prevSleepTime = sleepTime;
	uint32_t realTime = 0;
	setTimeStart();
	do
	{
		int diffRaster = DiffTimeRaster();
		sleepTime = int(std::max(int64_t(0), std::min(int64_t(m_frameTime), int64_t(sleepTime) + diffRaster)));
		setTimeEnd();
		realTime = DiffTime();

	} while (realTime <= (uint32_t) sleepTime);

	m_tickSync = m_tickEnd;

	// LOG(2,"[MiSTer] Frame %d Sleep prev=%d/final=%d/real=%d (frameTime=%d blitTime=%d emulationTime=%d) (vcount_vsync=%d/%d vcount_gpu=%d/%d)\n", m_frame, prevSleepTime, sleepTime, realTime, m_frameTime, m_streamTime, m_emulationTime, fpga.frameEcho, fpga.vCountEcho, fpga.frame, fpga.vCount);

	if (((uint32_t) sleepTime + 10000 < realTime)) //sleep?
	{
		LOG(1,"[MiSTer] Frame %d Sleep prev=%d/final=%d/real=%d (frameTime=%d blitTime=%d emulationTime=%d) (vcount_vsync=%d/%d vcount_gpu=%d/%d)\n", m_frame, prevSleepTime, sleepTime, realTime, m_frameTime, m_streamTime, m_emulationTime, fpga.frameEcho, fpga.vCountEcho, fpga.frame, fpga.vCount);
	}
}

int GroovyMister::DiffTimeRaster(void)
{		  
	uint32_t frameEcho = fpga.frameEcho;
	int diffTime = 0;
	if (m_frame != fpga.frameEcho)
	{
		getACK(0);
	}
	if (fpga.frameEcho != frameEcho)
	{
		//patch if emulator freezes to align frame counter
		/*
		if ((fpga.frameEcho + 1) < fpga.frame)
		{
			LOG(2,"[MiSTer] patch %d (patched=%d) %d / %d %d \n", fpga.frameEcho, fpga.frame + 1, fpga.vCountEcho, fpga.frame, fpga.vCount);
			fpga.frameEcho = fpga.frame + 1;
		}*/
		LOG(2,"[MiSTer] echo %d %d / %d %d \n", fpga.frameEcho, fpga.vCountEcho, fpga.frame, fpga.vCount);
		uint32_t vCount1 = ((fpga.frameEcho - 1) * m_vTotal + fpga.vCountEcho) >> m_interlace;
		uint32_t vCount2 = (fpga.frame * m_vTotal + fpga.vCount) >> m_interlace;
		int dif = (int) (vCount1 - vCount2) / 2; //dicotomic

		diffTime = (int) (m_widthTime * dif);
	}
	return diffTime;
}

void GroovyMister::BindInputs(const char* misterHost, uint16_t misterPort)
{
	if (!m_isConnected) return;
	// Set server
	m_serverAddrInputs.sin_family = AF_INET;
	m_serverAddrInputs.sin_port = htons(misterPort);
	if (inet_pton(AF_INET, misterHost, &m_serverAddrInputs.sin_addr) != 1) return;
	// Set socket
#ifdef _WIN32
	int rc;
	m_sockInputsFD = INVALID_SOCKET;
	LOG(0, "[MiSTer][Inputs] Initialising socket %s...\n","");
	m_sockInputsFD = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_sockInputsFD == INVALID_SOCKET)
	{
		LOG(0,"[MiSTer][Inputs] Could not create socket : %lu", ::GetLastError());
	}
	LOG(0,"[MiSTer][Inputs] Setting socket async %s...\n","");
	u_long iMode=1;
	rc = ioctlsocket(m_sockInputsFD, FIONBIO, &iMode);
	if (rc < 0)
	{
		LOG(0,"[MiSTer][Inputs] set nonblock fail %d\n", rc);
	}
	LOG(0,"[MiSTer][Inputs] Binding port %s...\n","");
		sendto(m_sockInputsFD, m_bufferSend, 1, 0, (struct sockaddr *)&m_serverAddrInputs, sizeof(m_serverAddrInputs));

#else
	printf("[MiSTer][Inputs] Initialising socket...\n");
	m_sockInputsFD = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (m_sockInputsFD < 0)
	{
		LOG(0,"[MiSTer][Inputs] Could not create socket : %d", m_sockInputsFD);
	}
	LOG(0,"[MiSTer][Input] Setting socket async %s...\n","");
	// Non blocking socket
	int flags;
	flags = fcntl(m_sockInputsFD, F_GETFL, 0);
	if (flags < 0)
	{
		LOG(0,"[MiSTer][Inputs] get falg error %d\n", flags);
	}
	flags |= O_NONBLOCK;
	if (fcntl(m_sockInputsFD, F_SETFL, flags) < 0)
	{
		LOG(0,"[MiSTer] set nonblock fail %d\n", flags);
	}
	LOG(0,"[MiSTer][Inputs] Binding port %s...\n","");
	sendto(m_sockInputsFD, m_bufferSend, 1, 0, (struct sockaddr *)&m_serverAddrInputs, sizeof(m_serverAddrInputs));
#endif
}

void GroovyMister::PollInputs(void)
{
#ifdef _WIN32
    if (m_sockInputsFD == INVALID_SOCKET) return;
#else
    if (m_sockInputsFD < 0) return;
#endif
	uint32_t joyFrame = joyInputs.joyFrame;
	uint8_t  joyOrder = joyInputs.joyOrder;
	uint32_t ps2Frame = ps2Inputs.ps2Frame;
	uint8_t  ps2Order = ps2Inputs.ps2Order;
	socklen_t sServerAddr = sizeof(struct sockaddr);
	int len = 0;
	do
	{
		len = recvfrom(m_sockInputsFD, m_bufferInputsReceive, sizeof(m_bufferInputsReceive), 0, (struct sockaddr *)&m_serverAddrInputs, &sServerAddr);
		if (len == 9 || len == 17) //blit joystick digital or analog
		{
			memcpy(&joyFrame, &m_bufferInputsReceive[0], 4);
			memcpy(&joyOrder, &m_bufferInputsReceive[4], 1);
			if (joyFrame > joyInputs.joyFrame || (joyFrame == joyInputs.joyFrame && joyOrder > joyInputs.joyOrder))
			{
				setFpgaJoystick(len);
			}
		}
		if (len == 37 || len == 41) //blit ps2 keyboard and mouse
		{
			memcpy(&ps2Frame, &m_bufferInputsReceive[0], 4);
			memcpy(&ps2Order, &m_bufferInputsReceive[4], 1);
			if (ps2Frame > ps2Inputs.ps2Frame || (ps2Frame == ps2Inputs.ps2Frame && ps2Order > ps2Inputs.ps2Order))
			{
				setFpgaPS2(len);
			}
		}
	} while (len > 0);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// PRIVATE
//
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
template <typename TV, typename TM>
inline TV RoundDown(TV Value, TM Multiple)
{
	return((Value / Multiple) * Multiple);
}

template <typename TV, typename TM>
inline TV RoundUp(TV Value, TM Multiple)
{
	return(RoundDown(Value, Multiple) + (((Value % Multiple) > 0) ? Multiple : 0));
}
#endif

//get aligned memory
char *GroovyMister::AllocateBufferSpace(const DWORD bufSize, const DWORD bufCount, DWORD& totalBufferSize, DWORD& totalBufferCount)
{
#ifdef _WIN32
	SYSTEM_INFO systemInfo;
	::GetSystemInfo(&systemInfo);

	const unsigned __int64 granularity = systemInfo.dwAllocationGranularity;
	const unsigned __int64 desiredSize = bufSize * bufCount;
	unsigned __int64 actualSize = RoundUp(desiredSize, granularity);

	if (actualSize > std::numeric_limits<DWORD>::max())
	{
		actualSize = (std::numeric_limits<DWORD>::max() / granularity) * granularity;
	}

	totalBufferCount = std::min<DWORD>(bufCount, static_cast<DWORD>(actualSize / bufSize));
	totalBufferSize = static_cast<DWORD>(actualSize) ;
	char *lBuffer = reinterpret_cast<char *>(VirtualAllocEx(GetCurrentProcess(), 0, totalBufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

	if (lBuffer == 0)
	{
		LOG(0,"[MiSTer] VirtualAllocEx Error %lu\n", ::GetLastError());
	}

	return lBuffer;
#else
	totalBufferSize = bufSize;
	char *lBuffer = (char*)malloc((size_t)bufSize);
	return lBuffer;
#endif
}


bool GroovyMister::Send(const void* cmd, int cmdSize)
{
    int sent = send(m_sockFD, static_cast<const char*>(cmd), cmdSize, 0);
    if (sent == cmdSize) return true;
#ifdef _WIN32
    int error = WSAGetLastError();
#else
    int error = errno;
#endif
    LOG(0, "[MiSTer] UDP send failed (%d), dropping transfer\n", error);
    return false;
}

void GroovyMister::SendStream(uint8_t whichBuffer, uint8_t field, uint32_t bytesToSend, uint32_t cSize)
{
    const char* data = whichBuffer ? m_pBufferAudio : cSize ? m_pBufferLZ4[field] : m_pBufferBlit[field];
    for (uint32_t offset = 0; offset < bytesToSend; offset += m_mtu)
        if (!Send(data + offset, int(std::min(uint32_t(m_mtu), bytesToSend - offset)))) break;
}

bool GroovyMister::SendReliable(uint8_t kind, const char* data, uint32_t size, uint32_t frame,
                               uint8_t field, uint16_t vsync, uint8_t flags, uint32_t transfer)
{
    using namespace groovy_wire;
    if (!transfer) transfer = ++m_transfer;
    Header h = {m_session, transfer, size, frame, 0, vsync, kind, field, flags};
    std::vector<uint8_t> packet(HEADER + m_payload), parity(m_payload, 0);
    if (kind == CLOSE) { encode(packet.data(), h); return Send(packet.data(), HEADER); }
    const uint32_t chunks = (size + m_payload - 1) / m_payload;
    for (uint32_t i = 0; i < chunks; ++i) {
        uint32_t offset = i * m_payload, n = std::min(uint32_t(m_payload), size - offset);
        h.index = uint16_t(i); h.flags = flags;
        encode(packet.data(), h); memcpy(packet.data() + HEADER, data + offset, n);
        if (!Send(packet.data(), int(HEADER + n))) return false;
        if (kind != VIDEO && kind != AUDIO) continue;
        for (uint32_t j = 0; j < n; ++j) parity[j] ^= uint8_t(data[offset + j]);
        if ((i + 1) % FEC_GROUP == 0 || i + 1 == chunks) {
            h.index = uint16_t(i / FEC_GROUP * FEC_GROUP); h.flags = flags | PARITY;
            encode(packet.data(), h); memcpy(packet.data() + HEADER, parity.data(), m_payload);
            if (!Send(packet.data(), int(HEADER + m_payload))) return false;
            std::fill(parity.begin(), parity.end(), uint8_t(0));
        }
    }
    return true;
}

inline void GroovyMister::setTimeStart(void)
{
#ifdef _WIN32
	QueryPerformanceCounter(&m_tickStart);
#else
	clock_gettime(CLOCK_MONOTONIC, &m_tickStart);
#endif
}

inline void GroovyMister::setTimeEnd(void)
{
#ifdef _WIN32
	QueryPerformanceCounter(&m_tickEnd);
#else
	clock_gettime(CLOCK_MONOTONIC, &m_tickEnd);
#endif
}

uint32_t GroovyMister::DiffTime(void)
{
#ifdef _WIN32
	return uint32_t((m_tickEnd.QuadPart - m_tickStart.QuadPart) * 10000000 / m_tickFrequency.QuadPart);
#else
	uint32_t diffTime = 0;
	timespec temp;
	if ((m_tickEnd.tv_nsec - m_tickStart.tv_nsec) < 0)
	{
		temp.tv_sec = m_tickEnd.tv_sec - m_tickStart.tv_sec - 1;
		temp.tv_nsec = 1000000000 + m_tickEnd.tv_nsec - m_tickStart.tv_nsec;
	}
	else
	{
		temp.tv_sec = m_tickEnd.tv_sec - m_tickStart.tv_sec;
		temp.tv_nsec = m_tickEnd.tv_nsec - m_tickStart.tv_nsec;
	}
	diffTime = (temp.tv_sec * 1000000000) + temp.tv_nsec;
	return diffTime / 100;
#endif
}

void GroovyMister::setFpgaStatus(void)
{
	uint8_t fpgaBits;
	memcpy(&fpga.frameEcho, &m_bufferReceive[0], 4);
	memcpy(&fpga.vCountEcho, &m_bufferReceive[4], 2);
	memcpy(&fpga.frame, &m_bufferReceive[6], 4);
	memcpy(&fpga.vCount, &m_bufferReceive[10], 2);
	memcpy(&fpgaBits, &m_bufferReceive[12], 1);

	bitByte bits;
	bits.byte = fpgaBits;
	fpga.vramReady     = bits.u.bit0;
	fpga.vramEndFrame  = bits.u.bit1;
	fpga.vramSynced    = bits.u.bit2;
	fpga.vgaFrameskip  = bits.u.bit3;
	fpga.vgaVblank     = bits.u.bit4;
	fpga.vgaF1         = bits.u.bit5;
	fpga.audio         = bits.u.bit6;
	fpga.vramQueue     = bits.u.bit7;

	LOG(2,"[MiSTer] ACK %d %d / %d %d / bits(%d%d%d%d%d%d%d%d)\n", fpga.frameEcho, fpga.vCountEcho, fpga.frame, fpga.vCount, fpga.vramReady, fpga.vramEndFrame, fpga.vramSynced, fpga.vgaFrameskip, fpga.vgaVblank, fpga.vgaF1, fpga.audio, fpga.vramQueue);
}

void GroovyMister::setFpgaJoystick(int len)
{
	memcpy(&joyInputs.joyFrame, &m_bufferInputsReceive[0], 4);
	memcpy(&joyInputs.joyOrder, &m_bufferInputsReceive[4], 1);
	memcpy(&joyInputs.joy1, &m_bufferInputsReceive[5], 2);
	memcpy(&joyInputs.joy2, &m_bufferInputsReceive[7], 2);
	LOG(2,"[MiSTer] JOY %d %d / %d %d\n", joyInputs.joyFrame, joyInputs.joyOrder, joyInputs.joy1, joyInputs.joy2);

	if (len == 17)
	{
		memcpy(&joyInputs.joy1LXAnalog, &m_bufferInputsReceive[9], 1);
		memcpy(&joyInputs.joy1LYAnalog, &m_bufferInputsReceive[10], 1);
		memcpy(&joyInputs.joy1RXAnalog, &m_bufferInputsReceive[11], 1);
		memcpy(&joyInputs.joy1RYAnalog, &m_bufferInputsReceive[12], 1);
		memcpy(&joyInputs.joy2LXAnalog, &m_bufferInputsReceive[13], 1);
		memcpy(&joyInputs.joy2LYAnalog, &m_bufferInputsReceive[14], 1);
		memcpy(&joyInputs.joy2RXAnalog, &m_bufferInputsReceive[15], 1);
		memcpy(&joyInputs.joy2RYAnalog, &m_bufferInputsReceive[16], 1);
		LOG(2,"[MiSTer] JOY A1(LX=%d,LY=%d,RX=%d,RY=%d) A2(LX=%d,LY=%d,RX=%d,RY=%d)\n", joyInputs.joy1LXAnalog, joyInputs.joy1LYAnalog, joyInputs.joy1RXAnalog, joyInputs.joy1RYAnalog, joyInputs.joy2LXAnalog, joyInputs.joy2LYAnalog, joyInputs.joy2RXAnalog, joyInputs.joy2RYAnalog);
	}
}

void GroovyMister::setFpgaPS2(int len)
{
	memcpy(&ps2Inputs.ps2Frame, &m_bufferInputsReceive[0], 4);
	memcpy(&ps2Inputs.ps2Order, &m_bufferInputsReceive[4], 1);

	if (m_verbose == 2)
	{
		LOG(2,"[MiSTer] KBD %d %d ", ps2Inputs.ps2Frame, ps2Inputs.ps2Order);
		for (int i=0; i<256; i++)
		{
			int bit_pre = 1 & (ps2Inputs.ps2Keys[i / 8] >> (i % 8));
			char *pos = &m_bufferInputsReceive[5];
			int bit_pos = 1 & (pos[i / 8] >> (i % 8));
			if (bit_pre != bit_pos)
			{
				LOG(2,"[%d=%d->%d]", i, bit_pre, bit_pos);
			}
		}
		LOG(2,"%s\n", "");
	}
	memcpy(&ps2Inputs.ps2Keys, &m_bufferInputsReceive[5], 32);

	if (len == 41)
	{
		memcpy(&ps2Inputs.ps2Mouse, &m_bufferInputsReceive[37], 1);
		memcpy(&ps2Inputs.ps2MouseX, &m_bufferInputsReceive[38], 1);
		memcpy(&ps2Inputs.ps2MouseY, &m_bufferInputsReceive[39], 1);
		memcpy(&ps2Inputs.ps2MouseZ, &m_bufferInputsReceive[40], 1);
		bitByte bits;
		bits.byte = ps2Inputs.ps2Mouse;
		LOG(2, "[MiSTer] MOUSE [yo=%d,xo=%d,ys=%d,xs=%d,1=%d,bm=%d,br=%d,bl=%d][x=%d,y=%d,z=%d]\n", bits.u.bit7, bits.u.bit6, bits.u.bit5, bits.u.bit4, bits.u.bit3, bits.u.bit2, bits.u.bit1, bits.u.bit0, ps2Inputs.ps2MouseX, ps2Inputs.ps2MouseY, ps2Inputs.ps2MouseZ);
	}
}

