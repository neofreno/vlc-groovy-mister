"""Export tracked + new, non-ignored working files; never commit or publish."""
import argparse
import json
import pathlib
import subprocess
import zipfile

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=pathlib.Path, help="New ZIP path (must not exist)")
args = parser.parse_args()
output = args.output.resolve()
if output.exists():
    parser.error("Output already exists; choose a new archive name")
listed = subprocess.check_output(
    ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"], cwd=root
)
names = sorted(set(p.decode("utf-8") for p in listed.split(b"\0") if p))
# Catch inherited ignore rules that could silently omit imported build inputs.
manifest = json.loads((root / "Groovy_MiSTer/IMPORT_MANIFEST.json").read_text(encoding="utf-8-sig"))
expected = {
    "Groovy_MiSTer/" + record["destination"] for record in manifest
    if not (record["destination"].startswith("hps_linux/src/support/groovy/kernel/usr/include/")
            and record["destination"].endswith(".cmd"))
}
missing = expected - set(names)
if missing:
    raise SystemExit("Imported inputs missing from Git export: " + ", ".join(sorted(missing)))
files = []
for name in names:
    path = root / name
    if not path.is_file():
        raise SystemExit(f"Missing indexed file: {name}")
    if path.is_symlink() or not path.resolve().is_relative_to(root):
        raise SystemExit(f"External or symlink input rejected: {name}")
    if path.resolve() == output:
        raise SystemExit("Output cannot be an input")
    files.append((name, path))
output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(output, "x", compression=zipfile.ZIP_DEFLATED) as archive:
    for name, path in files:
        archive.write(path, name)
print(f"Exported {len(files)} files: {output} ({output.stat().st_size} bytes)")
