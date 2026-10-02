"""Package a clean, committed release tree; never build, execute, or upload binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import zipfile

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("version")
parser.add_argument("output", type=Path, help="New output directory")
args = parser.parse_args()
if not args.version or any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-" for c in args.version):
    parser.error("Unsafe version label")
out = args.output.resolve()
if out.exists():
    parser.error("Output exists; use a new directory")

def git(*argv):
    return subprocess.check_output(["git", *argv], cwd=root)

if git("status", "--porcelain").strip():
    raise SystemExit("Commit release sources/docs first; working tree must be clean")
commit = git("rev-parse", "HEAD").decode().strip()
artifacts = {
    "windows-x64": [("x64/Release/libgroovy_mister64_plugin.dll", "libgroovy_mister64_plugin.dll", "PE-x64")],
    "windows-x86": [("Release/libgroovy_mister_plugin.dll", "libgroovy_mister_plugin.dll", "PE-x86")],
    "receivers": [
        (f"Groovy_MiSTer/hps_linux/{name}", f"mister/{name}", "ELF-ARM-hard-float")
        for name in ("MiSTer_groovy", "MiSTer_groovy_XDP", "MiSTer_groovy_wifi")
    ],
}
common = [
    "INSTALACION_Y_USO.md", "GUIA_CONFIGURACION_MODULO.md", "LICENSE.md",
    "THIRD_PARTY_NOTICES.md", "AUDITORIA_LICENCIAS.md",
    f"RELEASE_NOTES_{args.version}.md", "third_party/README.md",
    "third_party/SOURCES.json", "third_party/BINARIES.json", "sdk/NOTICE.md",
    "sdk/COPYING.LIB", "Groovy_MiSTer/LICENSE",
    "Groovy_MiSTer/hps_linux/src/LICENSE", "Groovy_MiSTer/hps_linux/VARIANTES.md",
    "Groovy_MiSTer/api/lz4/LICENSE", "Groovy_MiSTer/hps_linux/src/lib/zstd/LICENSE",
    "Groovy_MiSTer/hps_linux/src/lib/zstd/COPYING",
]
common += [p.relative_to(root).as_posix() for p in sorted((root / "LICENSES").rglob("*")) if p.is_file()]
for name in common:
    if not (root / name).is_file():
        raise SystemExit(f"Missing distribution notice: {name}")

def digest(data):
    return hashlib.sha256(data).hexdigest()

records = {}
for package, entries in artifacts.items():
    records[package] = []
    for source, target, architecture in entries:
        data = (root / source).read_bytes()
        if architecture.startswith("PE-"):
            assert data[:2] == b"MZ", source
            offset = struct.unpack_from("<I", data, 60)[0]
            assert data[offset:offset+4] == b"PE\0\0", source
            expected = 0x8664 if architecture == "PE-x64" else 0x14c
            assert struct.unpack_from("<H", data, offset+4)[0] == expected, source
        else:
            assert data[:6] == b"\x7fELF\x01\x01", source
            assert struct.unpack_from("<H", data, 18)[0] == 40, source
            assert struct.unpack_from("<I", data, 36)[0] & 0x400, source
        records[package].append({"file": target, "architecture": architecture, "bytes": len(data), "sha256": digest(data)})

out.mkdir(parents=True)
prefix = f"vlc-groovy-mister-{args.version}"
sources_name = f"{prefix}-sources.zip"
subprocess.run(["git", "archive", "--format=zip", f"--output={out / sources_name}", commit], cwd=root, check=True)
# Verify source archives and prebuilt inputs survive Git's attributes unchanged.
with zipfile.ZipFile(out / sources_name) as archive:
    assert archive.testzip() is None
    for manifest, path_prefix in (("SOURCES.json", "third_party/"), ("BINARIES.json", "")):
        for record in json.loads(archive.read("third_party/" + manifest)):
            data = archive.read(path_prefix + record["file"])
            assert len(data) == record["bytes"] and digest(data) == record["sha256"], record["file"]

for package, entries in artifacts.items():
    manifest = {
        "release": args.version, "source_commit": commit,
        "source_archive": sources_name,
        "source_url": f"https://github.com/neofreno/vlc-groovy-mister/releases/download/{args.version}/{sources_name}",
        "toolchains": {"windows": "MSVC v143 14.42", "arm": "GNU Arm 10.2-2020.11"},
        "validation": "See release notes; no physical test of these freshly built artifacts; x86 first candidate",
        "artifacts": records[package],
    }
    target_zip = out / f"{prefix}-{package}.zip"
    with zipfile.ZipFile(target_zip, "x", compression=zipfile.ZIP_DEFLATED) as archive:
        for name in common:
            archive.write(root / name, name)
        for source, target, _ in entries:
            archive.write(root / source, target)
        archive.writestr("BUILD_MANIFEST.json", json.dumps(manifest, indent=2) + "\n")
    with zipfile.ZipFile(target_zip) as archive:
        assert archive.testzip() is None
        for record in records[package]:
            data = archive.read(record["file"])
            assert len(data) == record["bytes"] and digest(data) == record["sha256"]
shutil.copyfile(root / "INSTALACION_Y_USO.md", out / "INSTALACION_Y_USO.md")
checksums = []
for path in sorted(out.iterdir()):
    checksums.append(f"{digest(path.read_bytes())}  {path.name}")
(out / "SHA256SUMS.txt").write_text("\n".join(checksums) + "\n", encoding="utf-8")
print(f"Packaged {len(checksums)} assets + SHA256SUMS.txt from {commit}")
for package, items in records.items():
    for record in items:
        print(f"{package}: {record['sha256']}  {record['file']}")
