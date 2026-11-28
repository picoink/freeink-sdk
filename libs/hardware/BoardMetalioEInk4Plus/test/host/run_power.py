#!/usr/bin/env python3
from pathlib import Path
import subprocess,tempfile
here=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='metalio-power-') as tmp:
    exe=Path(tmp)/'power'
    subprocess.run(['c++','-std=c++17','-fsanitize=undefined',str(here/'power_wait.cpp'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
