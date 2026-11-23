#!/usr/bin/env python3
"""Validate NTP replies and host safety without contacting a time server."""
from pathlib import Path
import os
import subprocess
import tempfile
here = Path(__file__).resolve().parent
library = here.parents[1]
with tempfile.TemporaryDirectory(prefix="freeink-ntp-") as tmp:
    exe = Path(tmp) / "packet"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DFREEINK_HOST=1", "-I", str(library / "include"),
                    str(here / "packet.cpp"), str(library / "src/NetworkTimeSync.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    stubs = Path(tmp) / "stubs"
    for name in ("Arduino.h", "esp_random.h", "lwip/sockets.h", "lwip/netdb.h", "freertos/FreeRTOS.h", "freertos/task.h"):
        p = stubs / name; p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "' + str(here / "fakes.h") + '"\n')
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DARDUINO_ARCH_ESP32=1", "-fsanitize=undefined", "-I", str(stubs),
                    "-I", str(library / "include"), str(here / "lifecycle.cpp"),
                    str(library / "src/NetworkTimeSync.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
print("NTP packets, timeouts, cancellation and host safety passed")
