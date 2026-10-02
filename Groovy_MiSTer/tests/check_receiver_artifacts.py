"""Inspect compiled ARM artifacts and copied sources; never execute a receiver."""
import argparse
import hashlib
import pathlib
import re
import struct
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("build_root", type=pathlib.Path)
parser.add_argument("main_source", type=pathlib.Path)
args = parser.parse_args()
repo = pathlib.Path(__file__).resolve().parents[1]
deps = {}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

for variant, artifact in (("standard", "MiSTer_groovy"), ("xdp", "MiSTer_groovy_XDP"), ("wifi", "MiSTer_groovy_wifi")):
    tree = args.build_root / variant
    source = tree / "hps_linux/src"
    for original in (repo / "hps_linux/src/support/groovy").iterdir():
        if original.suffix in (".cpp", ".h"):
            assert digest(original) == digest(source / "support/groovy" / original.name), (variant, original)
    for original in (repo / "protocol").glob("*.h"):
        assert digest(original) == digest(tree / "protocol" / original.name), (variant, original)
    assert digest(args.main_source / "fpga_io.cpp") == digest(source / "fpga_io.cpp"), variant
    assert digest(repo / "hps_linux/src/Makefile") == digest(source / "Makefile"), variant
    built, published = source / artifact, repo / "hps_linux" / artifact
    assert digest(built) == digest(published), variant
    data = built.read_bytes()
    assert data[:6] == b"\x7fELF\x01\x01" and struct.unpack_from("<H", data, 18)[0] == 40, variant
    assert struct.unpack_from("<I", data, 36)[0] & 0x400, "ARM hard-float required"
    assert f"Groovy receiver profile={variant}".encode() + b"\0" in data, variant
    assert b"coherent-20260929\0" in data and b"[Groovy STOP] complete" in data, variant
    assert (b"[XDP][STOP]" in data) == (variant == "xdp"), variant
    output = subprocess.check_output(["readelf", "-d", str(built)], text=True)
    deps[variant] = set(re.findall(r"Shared library: \[([^\]]+)\]", output))
    assert ("libelf.so.1" in deps[variant]) == (variant == "xdp"), variant
    print(f"PASS: {artifact}: ARM hard-float, matching sources/hooks, {variant} profile, SHA256={digest(built)}")
assert deps["standard"] == deps["wifi"], "UDP variants must use the same shared dependencies"
# The previously validated XDP executable also directly needs the ARM loader,
# in addition to libelf. Neither dependency is linked into the UDP variants.
assert deps["xdp"] == deps["standard"] | {"libelf.so.1", "ld-linux-armhf.so.3"}, "Unexpected XDP shared dependencies"
print("PASS: transport-specific shared-library dependency isolation")
