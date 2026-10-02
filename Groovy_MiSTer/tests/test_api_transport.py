"""Run against the compiled api_client, checking actual UDP bytes at the receiver."""
import argparse
import socket
import struct
import subprocess
import threading

MAGIC = b"GMW2"
HEADER = struct.Struct("<4sIIIIHHBBBB")
SIZE = 720 * 288 * 3


def decompress(data):
    # Small independent LZ4 block decoder for the test fixture; no pip dependency.
    out = bytearray()
    pos = 0
    while pos < len(data):
        token = data[pos]
        pos += 1
        literal = token >> 4
        if literal == 15:
            while True:
                extra = data[pos]; pos += 1; literal += extra
                if extra != 255: break
        out += data[pos:pos + literal]; pos += literal
        if pos == len(data): break
        offset = int.from_bytes(data[pos:pos + 2], "little"); pos += 2
        assert offset and offset <= len(out)
        match = (token & 15) + 4
        if (token & 15) == 15:
            while True:
                extra = data[pos]; pos += 1; match += extra
                if extra != 255: break
        assert len(out) + match <= SIZE
        for _ in range(match): out.append(out[-offset])
    return bytes(out)


def run(client, modern, compression, mtu, lose_control=False):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if hasattr(socket, "SIO_UDP_CONNRESET"): sock.ioctl(socket.SIO_UDP_CONNRESET, False)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 16 * 1024 * 1024)
    sock.bind(("127.0.0.1", 0)); sock.settimeout(0.2)
    frames = []; audio = 0; errors = []; stop = threading.Event(); closed = threading.Event()
    expected = {f: bytes((i // 360 + f) % 256 for i in range(SIZE)) for f in range(1, 13)}
    probes = []; sessions = {}; transfers = {}; legacy = None; mode_commands = 0

    def status(peer, frame=0):
        try: sock.sendto(struct.pack("<IHIHB", frame, 312, frame, 12, 0x47), peer)
        except ConnectionResetError: pass

    def worker():
        nonlocal audio, legacy, mode_commands
        try:
            while not stop.is_set():
                try: packet, peer = sock.recvfrom(65536)
                except (socket.timeout, ConnectionResetError): continue
                if len(packet) == 16 and packet[0] == 2:
                    probes.append(packet)
                    if modern:
                        assert packet[5] == 2 and packet[12:] == MAGIC
                        payload, session = struct.unpack_from("<HI", packet, 6)
                        sessions[session] = payload
                        if not lose_control or len(probes) > 1:
                            sock.sendto(struct.pack("<4sIIBBH", MAGIC, session, 0, 0, 2, payload), peer)
                    else:
                        # A generic ACK must never enable the indexed protocol.
                        status(peer)
                    continue
                if packet.startswith(MAGIC):
                    assert modern
                    magic, session, transfer, total, frame, index, vsync, kind, field, flags, version = HEADER.unpack_from(packet)
                    assert version == 2 and session in sessions
                    payload = sessions[session]
                    assert len(packet) <= mtu - 28
                    if kind in (3, 4):
                        if kind == 3:
                            assert packet[28] == 3 and len(packet) == 54
                            mode_commands += 1
                        if kind == 4: closed.set()
                        if kind == 4 or not lose_control or mode_commands > 1:
                            sock.sendto(struct.pack("<4sIIBBH", MAGIC, session, transfer, kind, 2, payload), peer)
                        continue
                    assert kind in (1, 2) and not (flags & 0x7e)
                    if flags & 128:
                        assert len(packet) == 28 + payload and index % 8 == 0
                        continue
                    chunks = transfers.setdefault(transfer, {})
                    assert index not in chunks
                    chunks[index] = packet[28:]
                    count = (total + payload - 1) // payload
                    if len(chunks) == count:
                        raw = b"".join(chunks[i] for i in range(count))
                        assert len(raw) == total
                        if kind == 1:
                            assert flags == compression
                            raw = decompress(raw) if compression else raw
                            assert raw == expected[frame]
                            frames.append(frame); status(peer, frame)
                        else:
                            assert raw == bytes(3840); audio += 1
                    continue
                assert not modern
                if legacy is not None:
                    remaining = legacy["total"] - len(legacy["data"])
                    assert len(packet) == min(mtu - 28, remaining), (len(packet), remaining)
                    legacy["data"] += packet
                    if len(legacy["data"]) == legacy["total"]:
                        raw = bytes(legacy["data"])
                        if legacy["kind"] == 1:
                            raw = decompress(raw) if compression else raw
                            assert raw == expected[legacy["frame"]]
                            frames.append(legacy["frame"]); status(peer, legacy["frame"])
                        else:
                            assert raw == bytes(3840); audio += 1
                        legacy = None
                elif len(packet) == 5 and packet[0] == 2:
                    assert packet == bytes([2, compression, 3, 2, 0]); status(peer)
                elif len(packet) == 26 and packet[0] == 3:
                    mode_commands += 1
                elif packet[0] == 7:
                    assert len(packet) == (12 if compression else 8)
                    frame = struct.unpack_from("<I", packet, 1)[0]
                    total = struct.unpack_from("<I", packet, 8)[0] if compression else SIZE
                    legacy = dict(kind=1, frame=frame, total=total, data=bytearray())
                elif len(packet) == 3 and packet[0] == 4:
                    legacy = dict(kind=2, total=int.from_bytes(packet[1:], "little"), data=bytearray())
                else:
                    assert packet == b"\x01", packet[:20]
                    closed.set()
        except BaseException as exc:
            errors.append(exc)

    thread = threading.Thread(target=worker); thread.start()
    try:
        proc = subprocess.run([client, "127.0.0.1", str(sock.getsockname()[1]), str(compression), "12", str(mtu)],
                              capture_output=True, text=True, timeout=30)
        closed.wait(5)
    finally:
        stop.set(); thread.join(2); sock.close()
    assert not errors, errors
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert frames == list(range(1, 13)), frames
    assert audio == 12 and mode_commands >= 1, (audio, mode_commands, proc.stdout)
    assert ("Wi-Fi protocol v2" if modern else "Legacy protocol") in proc.stdout, proc.stdout
    print(f"PASS: {'v2' if modern else 'legacy'} LZ4={compression} MTU={mtu}, 12 exact video frames + {audio} audio blocks, retry={lose_control}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(); parser.add_argument("client"); args = parser.parse_args()
    for modern in (False, True):
        for compression in (0, 1):
            for mtu in (1500, 9000):
                run(args.client, modern, compression, mtu, lose_control=modern)
