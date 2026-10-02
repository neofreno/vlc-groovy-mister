"""API wire validation, localhost only; not VLC callbacks or the HPS receiver."""
import pathlib
import socket
import struct
import subprocess
import sys
import threading

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "Groovy_MiSTer" / "tests"))
from test_api_transport import decompress, HEADER


def run(client, modern, compression):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        sock.ioctl(socket.SIO_UDP_CONNRESET, False)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.1)
    stop, closed = threading.Event(), threading.Event()
    errors, frames, audios, rates = [], [], [], []
    session = payload = rate = modes = 0
    mode_ready = False
    chunks, legacy = {}, None
    epoch = None
    audio_offset = 0

    def status(peer, frame):
        sock.sendto(struct.pack("<IHIHB", frame, 240, frame, 0, 0x47), peer)

    def control(peer, kind, transfer):
        sock.sendto(struct.pack("<4sIIBBH", b"GMW2", session, transfer, kind, 2, payload), peer)

    def complete(peer, kind, frame, data):
        nonlocal audio_offset
        assert mode_ready, "media before fresh MODE"
        if kind == 1:
            raw = decompress(data) if compression else data
            phase = len(frames)
            assert phase < 6 and raw == bytes([phase + 1]) * (320 * 240 * 3)
            assert rate == [3, 3, 2, 2, 1, 3][phase], "wrong INIT sample rate"
            frames.append(phase)
            status(peer, frame)
        else:
            phase = frames[-1]
            assert phase in (1, 2, 4, 5)
            expected = struct.pack("<19200h", *[
                (((19996 + i) % 20000) * 17) % 16000 + phase * 1000 for i in range(19200)])
            assert len(data) == min(32768, len(expected) - audio_offset)
            assert data == expected[audio_offset:audio_offset + len(data)], "ring wrap or PCM chunk mismatch"
            assert frames[-1] == phase and rate == [3, 3, 2, 2, 1, 3][phase]
            audio_offset += len(data)
            if audio_offset == len(expected):
                audios.append(phase)
                audio_offset = 0

    def worker():
        nonlocal session, payload, rate, modes, mode_ready, legacy, epoch
        try:
            while not stop.is_set():
                try:
                    packet, peer = sock.recvfrom(65536)
                except (socket.timeout, ConnectionResetError):
                    continue
                if len(packet) in (5, 16) and packet[0] == 2:
                    if len(packet) != (16 if modern else 5):
                        continue
                    assert packet[1] == compression and packet[3:5] == bytes([2, 0])
                    if modern:
                        payload, session = struct.unpack_from("<HI", packet, 6)
                    identity = (peer, session)
                    if identity != epoch:
                        epoch = identity
                        rate = packet[2]
                        rates.append(rate)
                        mode_ready = False
                        chunks.clear()
                        legacy = None
                    control(peer, 0, 0) if modern else status(peer, 0)
                    continue
                if modern:
                    assert packet.startswith(b"GMW2")
                    _, sid, transfer, total, frame, index, _, kind, _, flags, version = HEADER.unpack_from(packet)
                    assert sid == session and version == 2
                    if kind in (3, 4):
                        if kind == 3:
                            mode_ready = True
                            modes += 1
                        control(peer, kind, transfer)
                        if kind == 4:
                            # INIT renegotiation also closes a session. Only the
                            # final CLOSE, after all media, can end this fixture.
                            if frames == list(range(6)) and audios == [1, 2, 4, 5]:
                                closed.set()
                        continue
                    assert kind in (1, 2)
                    if flags & 128:
                        continue
                    parts = chunks.setdefault(transfer, {})
                    parts[index] = packet[28:]
                    if len(parts) == (total + payload - 1) // payload:
                        data = b"".join(parts[i] for i in range(len(parts)))
                        del chunks[transfer]
                        assert len(data) == total
                        complete(peer, kind, frame, data)
                elif legacy is not None:
                    legacy["data"] += packet
                    assert len(legacy["data"]) <= legacy["total"]
                    if len(legacy["data"]) == legacy["total"]:
                        complete(peer, legacy["kind"], legacy["frame"], bytes(legacy["data"]))
                        legacy = None
                elif len(packet) == 26 and packet[0] == 3:
                    modes += 1
                    mode_ready = True
                elif packet == b"\x01":
                    if frames == list(range(6)) and audios == [1, 2, 4, 5]:
                        closed.set()
                elif packet[0] == 7:
                    frame = struct.unpack_from("<I", packet, 1)[0]
                    total = struct.unpack_from("<I", packet, 8)[0] if compression else 320 * 240 * 3
                    legacy = dict(kind=1, frame=frame, total=total, data=bytearray())
                else:
                    assert len(packet) == 3 and packet[0] == 4
                    legacy = dict(kind=2, frame=0, total=int.from_bytes(packet[1:], "little"), data=bytearray())
        except BaseException as exc:
            errors.append(exc)

    thread = threading.Thread(target=worker)
    thread.start()
    try:
        result = subprocess.run([client, str(sock.getsockname()[1]), str(compression)],
                                capture_output=True, text=True, timeout=20)
        final_close = closed.wait(2)
    finally:
        stop.set()
        thread.join(2)
        sock.close()
    assert not errors, errors
    assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
    assert final_close, "missing final CLOSE after complete audio/video"
    assert frames == list(range(6)) and audios == [1, 2, 4, 5], (frames, audios)
    assert rates == [3, 2, 1, 3] and modes == 4, (rates, modes)
    assert audio_offset == 0
    print(f"PASS: modern={modern} LZ4={compression}: six frames, four exact wrapped PCM reads in eight chunks, four INIT/MODE pairs")


if __name__ == "__main__":
    for modern in (False, True):
        for compression in (0, 1):
            run(sys.argv[1], modern, compression)
