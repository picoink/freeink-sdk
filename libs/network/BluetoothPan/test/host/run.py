#!/usr/bin/env python3
"""Compile capability combinations without Arduino or a Bluetooth runtime."""
from pathlib import Path
import os
import subprocess
import tempfile

here = Path(__file__).resolve().parent
flags = ["FREEINK_CAP_BLUETOOTH_PAN", "SOC_BT_CLASSIC_SUPPORTED",
         "CONFIG_BT_BLUEDROID_ENABLED", "CONFIG_BT_CLASSIC_ENABLED", "CONFIG_BT_PAN_ENABLED"]
with tempfile.TemporaryDirectory(prefix="freeink-pan-") as tmp:
    cases = [[], flags, *[[f for f in flags if f != missing] for missing in flags],
             [*flags, "FREEINK_HOST"]]
    for n, case in enumerate(cases):
        exe = Path(tmp) / str(n)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                        "-I", str(here.parents[1] / "include"),
                        *[f"-D{f}=1" for f in case], f"-DEXPECT_SUPPORTED={int(n == 1)}",
                        str(here / "capability.cpp"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
print("Bluetooth PAN capability matrix passed")
