"""Local UDP fixture for the real plugin wrapper. Never contacts MiSTer/VLC."""
import selectors
import socket
import struct
import subprocess
import sys
import threading


def main():
    sockets = []
    stop = threading.Event()
    errors = []
    registrations = []
    selector = selectors.DefaultSelector()
    try:
        # Wrapper uses these fixed ports. Fail, never interfere, if unavailable.
        for port in (32100, 32101):
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sockets.append(sock)
            sock.bind(("127.0.0.1", port))
            sock.setblocking(False)
            selector.register(sock, selectors.EVENT_READ, port)

        def serve():
            try:
                while not stop.is_set():
                    for key, _ in selector.select(0.05):
                        sock = key.fileobj
                        data, peer = sock.recvfrom(65536)
                        if key.data == 32100:
                            # Legacy INIT ACK (modern probe intentionally ignored).
                            if len(data) == 5 and data[0] == 2:
                                sock.sendto(struct.pack("<IHIHB", 0, 240, 0, 0, 0x47), peer)
                        else:
                            assert len(data) == 1, "invalid input subscription"
                            phase = len(registrations)
                            assert phase < 2, "duplicate subscription/socket"
                            registrations.append(peer)
                            sock.sendto(b"bad-size", peer)
                            packet = struct.pack("<IBHH", 1 if phase else 100000, 1,
                                                 32 if phase else 128, 1)
                            if phase:
                                packet += bytes(range(20, 28))
                            sock.sendto(packet, peer)
            except BaseException as error:
                errors.append(error)

        worker = threading.Thread(target=serve)
        worker.start()
        try:
            result = subprocess.run([sys.argv[1]], capture_output=True, text=True, timeout=15)
        finally:
            stop.set()
            worker.join(timeout=2)
        print(result.stdout, end="")
        assert not worker.is_alive(), "fixture thread did not stop"
        assert not errors, repr(errors)
        assert result.returncode == 0, f"client exit={result.returncode}: {result.stderr}"
        assert len(registrations) == 2, "missing subscription after reconnect"
    finally:
        selector.close()
        for sock in sockets:
            sock.close()


if __name__ == "__main__":
    main()
