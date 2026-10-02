"""Real API + production watchdog against a restarted UDP peer (loopback only)."""
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[2] / "Groovy_MiSTer" / "tests"))
from test_api_transport import decompress

HEADER = struct.Struct("<4sIIIIHHBBBB")
SIZE = 320 * 240 * 3


def run(client, modern, compression):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        sock.ioctl(socket.SIO_UDP_CONNRESET, False)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.1)
    stop = threading.Event()
    errors = []
    frames = [[], []]
    mode_count = [0, 0]
    epochs = []
    outage = None
    outage_peer = None
    resumed = False
    mode_ready = False
    session = 0
    payload = 0
    chunks = {}
    legacy = None
    retry_seen = False
    last_duplicate = 0

    def status(peer, frame):
        sock.sendto(struct.pack("<IHIHB", frame, 240, frame, 0, 0x47), peer)

    def control(peer, kind, transfer):
        sock.sendto(struct.pack("<4sIIBBH", b"GMW2", session, transfer, kind, 2, payload), peer)

    def complete(peer, frame, data):
        nonlocal outage, outage_peer, mode_ready, chunks, legacy
        assert mode_ready, "media before MODE"
        raw = decompress(data) if compression else data
        assert raw == bytes([0x55 if resumed else 0xAA]) * SIZE
        frames[int(resumed)].append(frame)
        status(peer, frame)
        if not resumed and len(frames[0]) == 3:
            outage = time.monotonic()
            outage_peer = peer
            mode_ready = False
            chunks = {}
            legacy = None

    def worker():
        nonlocal outage, resumed, mode_ready, session, payload, chunks, legacy, retry_seen, last_duplicate
        try:
            while not stop.is_set():
                try:
                    packet, peer = sock.recvfrom(65536)
                except (socket.timeout, ConnectionResetError):
                    continue
                is_init = len(packet) in (5, 16) and packet[0] == 2
                if outage is not None and not resumed:
                    if is_init:
                        retry_seen = True
                    if time.monotonic() - outage < 2.5 or not is_init:
                        # Duplicate ACKs are traffic, not evidence of progress.
                        if peer == outage_peer and not is_init and time.monotonic() - last_duplicate > 0.05:
                            status(peer, 3)
                            last_duplicate = time.monotonic()
                        continue
                    resumed = True
                    mode_ready = False
                if is_init:
                    if modern:
                        if len(packet) != 16:
                            continue
                        payload, session = struct.unpack_from("<HI", packet, 6)
                        if not epochs or epochs[-1] != session:
                            epochs.append(session)
                        control(peer, 0, 0)
                    elif len(packet) == 5:
                        if not epochs or epochs[-1] != peer:
                            epochs.append(peer)
                        status(peer, 0)
                    continue
                if packet.startswith(b"GMW2"):
                    assert modern
                    _, sid, transfer, total, frame, index, _, kind, _, flags, version = HEADER.unpack_from(packet)
                    assert sid == session and version == 2, "old session after restart"
                    if kind == 4:
                        control(peer, kind, transfer)
                        continue
                    if kind == 3:
                        assert len(packet) == 54
                        mode_ready = True
                        mode_count[int(resumed)] += 1
                        control(peer, kind, transfer)
                        continue
                    assert kind == 1 and mode_ready, "video before fresh INIT/MODE"
                    if flags & 128:
                        continue
                    parts = chunks.setdefault(transfer, {})
                    parts[index] = packet[28:]
                    count = (total + payload - 1) // payload
                    if len(parts) == count:
                        data = b"".join(parts[i] for i in range(count))
                        del chunks[transfer]
                        complete(peer, frame, data)
                    continue
                assert not modern
                if legacy is not None:
                    legacy["data"] += packet
                    assert len(legacy["data"]) <= legacy["total"]
                    if len(legacy["data"]) == legacy["total"]:
                        frame, data = legacy["frame"], bytes(legacy["data"])
                        legacy = None
                        complete(peer, frame, data)
                elif len(packet) == 26 and packet[0] == 3:
                    mode_ready = True
                    mode_count[int(resumed)] += 1
                elif packet == b"\x01":
                    continue
                else:
                    assert mode_ready and packet[0] == 7
                    frame = struct.unpack_from("<I", packet, 1)[0]
                    total = struct.unpack_from("<I", packet, 8)[0] if compression else SIZE
                    legacy = dict(frame=frame, total=total, data=bytearray())
        except BaseException as exc:
            errors.append(exc)

    thread = threading.Thread(target=worker)
    thread.start()
    try:
        result = subprocess.run([client, str(sock.getsockname()[1]), str(compression)],
                                capture_output=True, text=True, timeout=20)
    finally:
        stop.set()
        thread.join(2)
        sock.close()
    assert not errors, errors
    assert result.returncode == 0, result.stdout + result.stderr
    assert resumed and retry_seen and len(epochs) == 2, (resumed, retry_seen, epochs)
    assert all(mode_count) and frames[0][:3] == [1, 2, 3] and frames[1][:3] == [1, 2, 3], (mode_count, frames)
    print(f"PASS: {'v2' if modern else 'legacy'} LZ4={compression}: 2.5 s outage, duplicate ACKs, fresh session/mode and exact pixels")


if __name__ == "__main__":
    for modern in (False, True):
        for compression in (0, 1):
            run(sys.argv[1], modern, compression)
