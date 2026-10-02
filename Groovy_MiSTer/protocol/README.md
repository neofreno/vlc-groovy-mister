# GroovyMiSTer UDP compatibility

The C++ API keeps the existing method signatures. Windows uses bounded synchronous
UDP sends, so a caller can reuse a buffer after a call returns. Calls on one
instance must be serialized by the caller. `Create` and `Destroy` allocate and
release the object inside the library; consumers should use them instead of
allocating the class with a possibly outdated header.

## Legacy compatibility with older receivers

No server update is required. If v2 negotiation receives no matching ACK, the API
uses the original five-byte INIT, command formats and payload size. An IP MTU of
1500 produces 1472-byte UDP payloads; MTU 9000 produces 8972-byte payloads. Jumbo
frames must also be enabled on the server and supported by the network. XDP still
requires its Ethernet interface; changing the API cannot make an Ethernet XDP
program receive Wi-Fi traffic.

## Protocol v2 (all rebuilt receiver variants)

All multibyte fields are little endian. The old experimental two-byte-index
protocol is replaced, not negotiated: use the rebuilt HPS and updated API for v2.
The current standard, XDP and Wi-Fi builds share this protocol and also accept
legacy clients. The name "Wi-Fi v2" in older logs does not restrict it to WLAN.
Their transport/tuning differences are documented in [VARIANTES.md](../hps_linux/VARIANTES.md).

INIT is 16 bytes: legacy bytes 0..4, version 2 at byte 5, data payload size (u16)
at 6, random session ID (u32) at 8, and `GMW2` at 12. The API retries up to three
times at 120 ms. An identical retry is acknowledged without resetting the server.
Only a 16-byte ACK with matching magic, session, version, kind and payload enables
v2. Generic legacy status replies do not enable it.

ACK layout: `GMW2`, session u32, transfer u32, kind u8, version u8, payload u16.
Modeline and close commands use numbered v2 transfers and are acknowledged;
repeated control messages are idempotent. A modeline fences off older media.

Media/control datagram header (28 bytes):

| Offset | Field |
| --- | --- |
| 0 | `GMW2` |
| 4 | Session u32 |
| 8 | Transfer sequence u32 (nonzero) |
| 12 | Total data bytes u32 |
| 16 | Video frame u32 |
| 20 | Chunk index u16 |
| 22 | Requested vsync u16 |
| 24 | Kind: video 1, audio 2, modeline 3, close 4 |
| 25 | Video field |
| 26 | Flags: LZ4 bit 0, XOR parity bit 7 |
| 27 | Version 2 |

MTU 1500 leaves 1444 data bytes. The header is repeated in every packet, allowing
recovery even if the first packet is lost. Video and audio have independent
assemblers. Each set of eight data chunks has one full-payload XOR parity packet,
whose index is the first index in its group; the last data chunk is zero-padded
for parity calculation. This recovers any one missing chunk in each group.

Duplicate, stale, malformed and foreign-session packets are discarded. Assemblies
expire after 80 ms from their first packet, even when traffic continues. A newer
transfer supersedes an incomplete one. Only complete video/audio reaches the
FPGA. Video frames use independent LZ4 blocks, without deltas referring to a frame
that might have been lost. Unrecoverable frames are skipped rather than decoded
with missing bytes. FEC costs up to one extra datagram per eight chunks and does
not compensate for a link whose sustained bandwidth is below the video bitrate.

Limits follow the current FPGA DDR layout: 720*576*4 bytes per video field,
32768 bytes per audio transfer, and an IP MTU between 1500 and 9000. Oversized
modelines are rejected. After modeline failure `getPBufferBlit` returns null.

## Build and test

Windows: `powershell -File api/build-msvc.ps1`. The library and matching header are
in `api/build/x64/Release`; `api/libgroovymister.lib` is also refreshed. The VLC
solution references this project and rebuilds it automatically.

HPS: with the ARM 10.2 toolchain on PATH, run
`bash hps_linux/build-wifi.sh /path/to/complete/Main_MiSTer /path/to/separate/build`.
The repository's `hps_linux/src` is an overlay, not a complete Main_MiSTer checkout.
The complete checkout must be the matching baseline used with this overlay.

Tests: compile `tests/protocol_tests.cpp`, `tests/api_lifecycle.cpp`, and
`tests/api_client.cpp` (the latter two link the API and Winsock on Windows).
Run `python tests/test_api_transport.py /path/to/api_client.exe` to check real UDP
packets, compressed and raw pixel contents, audio, jumbo payloads, control retries
and legacy fallback. The reassembly test also runs under ASan/UBSan on Linux.
