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
        subprocess.run([str(exe)], check=True, timeout=30)
    exe = Path(tmp) / "protocol"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DFREEINK_HOST=1", "-I", str(here.parents[1] / "include"),
                    str(here / "protocol.cpp"), str(here.parents[1] / "src/BluetoothPan.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
    stubs = Path(tmp) / "stubs"
    names = ["sdkconfig.h", "soc/soc_caps.h", "Arduino.h", "Network.h", "Preferences.h",
             "esp32-hal-bt.h", "esp_bt.h", "esp32-hal-alloc-bt-classic-mem.h", "esp_bt_main.h", "esp_bt_device.h",
             "esp_gap_bt_api.h", "esp_pan_api.h", "esp_netif.h", "esp_netif_defaults.h",
             "freertos/FreeRTOS.h", "freertos/task.h", "freertos/queue.h"]
    for name in names:
        header = stubs / name
        header.parent.mkdir(parents=True, exist_ok=True)
        header.write_text('#include "' + str(here / "fakes.h") + '"\n')
    exe = Path(tmp) / "lifecycle"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DFREEINK_CAP_BLUETOOTH_PAN=1", "-DARDUINO_ARCH_ESP32=1",
                    "-fsanitize=" + os.environ.get("SANITIZERS", "undefined"), "-fno-omit-frame-pointer",
                    "-I", str(stubs), "-I", str(here.parents[1] / "include"),
                    "-I", str(here.parents[2] / "BluetoothHost/include"),
                    str(here.parents[2] / "BluetoothHost/src/BluetoothHost.cpp"),
                    str(here / "lifecycle.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
    exe = Path(tmp) / "leases"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
                    "-DARDUINO_ARCH_ESP32=1", "-I", str(stubs),
                    "-I", str(here.parents[2] / "BluetoothHost/include"),
                    str(here.parents[2] / "BluetoothHost/src/BluetoothHost.cpp"),
                    str(here.parents[2] / "BluetoothHost/test/host/leases.cpp"), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
print("Bluetooth PAN capability, framing, retry, lifecycle and bounded RX tests passed")
