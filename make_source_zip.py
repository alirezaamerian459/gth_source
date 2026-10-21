"""Builds the complete source archive of Game Transfer Hub: ../GTH_source.zip (next to this folder).

Contents: every root-level file (CMakeLists.txt, docs, build scripts, the NSIS script, README_SOURCE.md),
plus src/, tests/ and release/ - without binaries or build output (no .exe/.zip/.lib/.pdb..., no build/ folder).
Run from anywhere:  python make_source_zip.py [output.zip]
"""
import hashlib
import os
import sys
import zipfile

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(ROOT), "GTH_source.zip")

SKIP_DIRS = {"build", "Release", "Debug", ".vs", "x64", "__pycache__"}
SKIP_EXT = (".obj", ".pdb", ".ilk", ".exe", ".zip", ".lib", ".dll", ".idb", ".tlog")
TEXT_EXT = (".cpp", ".h", ".hpp", ".txt", ".md", ".rc", ".bat", ".nsi", ".cmake", ".js", ".manifest", ".py")

if os.path.exists(OUT):
    os.remove(OUT)

entries = []
for name in sorted(os.listdir(ROOT)):
    path = os.path.join(ROOT, name)
    if os.path.isfile(path) and not name.lower().endswith(SKIP_EXT):
        entries.append((path, name))
for top in ("src", "tests", "release"):
    for folder, dirs, files in os.walk(os.path.join(ROOT, top)):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        for f in sorted(files):
            if f.lower().endswith(SKIP_EXT):
                continue
            path = os.path.join(folder, f)
            entries.append((path, os.path.relpath(path, ROOT).replace(os.sep, "/")))

with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
    for path, arc in entries:
        z.write(path, arc)

problems = []
with zipfile.ZipFile(OUT) as z:
    names = z.namelist()
    if z.testzip() is not None:
        problems.append("zip integrity")
    for path, arc in entries:
        data = z.read(arc)
        if data != open(path, "rb").read():
            problems.append("differs from the folder: " + arc)
        if arc.lower().endswith(TEXT_EXT) and b"\x00" in data:
            problems.append("NUL bytes in " + arc)
    for must in ("CMakeLists.txt", "README_SOURCE.md", "GameTransferHub.nsi", "src/app/src/UpdatesView.cpp",
                 "src/network/src/SonyLinkImport.cpp", "tests/unit/test_sony_link_import.cpp",
                 "release/UPDATES_GUIDE.md", "release/gth-sony-link-collector.user.js"):
        if must not in names:
            problems.append("missing " + must)
    binaries = [n for n in names if n.lower().endswith(SKIP_EXT)]
    if binaries:
        problems.append("binaries included: " + str(binaries))

print("entries:", len(names), "| size:", os.path.getsize(OUT), "| sha256:", hashlib.sha256(open(OUT, "rb").read()).hexdigest().upper())
print("problems:", problems if problems else "none")
sys.exit(1 if problems else 0)
