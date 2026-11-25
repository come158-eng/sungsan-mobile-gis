#!/usr/bin/env python3
"""Exercise native field inputs with the same ZIP extractor and LandStar parser as the APK."""
import hashlib
from pathlib import Path
import shlex
import subprocess
import tempfile
import zipfile

root = Path(__file__).resolve().parents[1]
qt = next(name for name in ("Qt6Core", "Qt5Core") if subprocess.run(
    ["pkg-config", "--exists", name], check=False).returncode == 0)
flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", qt, "libzip"], text=True))

with tempfile.TemporaryDirectory() as tmp:
    folder = Path(tmp)
    landstar = folder / "landstar"
    subprocess.run(["g++", "-std=c++17", "-fPIC", "-I" + str(root / "src/core"),
                    str(root / "test/test_landstarpointfile.cpp"), "-o", str(landstar), *flags], check=True)
    subprocess.run([str(landstar)], check=True)
    print("PASS: native LandStar CSV, TXT, sparse TSV, axis order, RTK/accuracy, duplicate and geographic validation")
    source = folder / "extract.cpp"
    source.write_text('''
#include "utils/projectarchive.h"
#include <cstdio>
int main(int argc, char **argv) {
  if (argc != 3) return 2;
  QStringList files;
  const QString error = ProjectArchive::extract(QString::fromUtf8(argv[1]), QString::fromUtf8(argv[2]), files, true);
  if (!error.isEmpty()) { std::fprintf(stderr, "%s\\n", qPrintable(error)); return 1; }
  return 0;
}
''', encoding="utf-8")
    binary = folder / "extract"
    subprocess.run(["g++", "-std=c++17", "-fPIC", "-I" + str(root / "src/core"),
                    str(source), "-o", str(binary), *flags], check=True)
    target = folder / "project"
    target.mkdir()
    archive = folder / "field.zip"
    payload = bytes(range(256)) * 8192
    with zipfile.ZipFile(archive, "w") as z:
        z.writestr("field/1111.qgz", b"project", zipfile.ZIP_DEFLATED)
        z.writestr("field/data/field_data.gpkg", payload, zipfile.ZIP_DEFLATED)
        z.writestr("field/photos/empty/", b"")
        z.writestr("field/photos/picture.jpg", b"picture-payload-only-once", zipfile.ZIP_STORED)
    def extract(path):
        return subprocess.run([str(binary), str(path), str(target)], check=False).returncode
    assert extract(archive) == 0
    assert (target / "field/1111.qgz").read_bytes() == b"project"
    assert (target / "field/photos/empty").is_dir()
    assert hashlib.sha256((target / "field/data/field_data.gpkg").read_bytes()).digest() == hashlib.sha256(payload).digest()
    photo = target / "field/photos/picture.jpg"
    photo.write_bytes(b"earlier-photo")
    bad = folder / "interrupted.zip"
    bad.write_bytes(archive.read_bytes()[:-48])
    assert extract(bad) != 0
    bad.write_bytes(archive.read_bytes().replace(b"picture-payload-only-once", b"Picture-payload-only-once"))
    assert extract(bad) != 0 and photo.read_bytes() == b"earlier-photo"
    with zipfile.ZipFile(bad, "w") as z:
        z.writestr("field/photos/picture.jpg/child", b"blocked destination")
    assert extract(bad) != 0 and photo.read_bytes() == b"earlier-photo"
    assert extract(archive) == 0 and photo.read_bytes() == b"picture-payload-only-once"
print("PASS: native streaming extraction, complete writes, corrupt/blocked item preservation, retry")
