#!/usr/bin/env python3
"""Compile and execute the production photo transaction using host Qt Core."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
qt = next((name for name in ('Qt6Core', 'Qt5Core') if subprocess.run(
    ['pkg-config', '--exists', name], check=False).returncode == 0), None)
if not qt:
    raise SystemExit('Host Qt Core development package required')
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', qt], text=True))
with tempfile.TemporaryDirectory() as folder:
    executable = str(Path(folder)/'photo-store-test')
    subprocess.run(['g++', '-std=c++17', '-fPIC', '-I'+str(root/'src/core'),
                    str(root/'test/test_managedphotostore.cpp'), '-o', executable, *flags], check=True)
    subprocess.run([executable], check=True)
