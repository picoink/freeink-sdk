#!/usr/bin/env python3
"""Run the production EPDiy worker against a controllable panel and task scheduler."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
LIB = HERE.parents[1]
with tempfile.TemporaryDirectory(prefix="freeink-epdiy-") as directory:
    root = Path(directory)
    for folder in ("src/driver", "src/bus", "include", "freertos"):
        (root / folder).mkdir(parents=True, exist_ok=True)
    for name in ("EpdiyDriver.cpp", "EpdiyDriver.h", "PanelDriver.h"):
        shutil.copy2(LIB / "src/driver" / name, root / "src/driver" / name)
    for name in ("EpdiyConfig.h", "GrayscaleCapabilities.h", "RefreshMetrics.h"):
        shutil.copy2(LIB / "include" / name, root / "include" / name)
    shutil.copy2(HERE / "pro_stubs/EpdBus.h", root / "src/bus/EpdBus.h")
    for source in (HERE / "epdiy_stubs").rglob("*.h"):
        shutil.copy2(source, root / source.relative_to(HERE / "epdiy_stubs"))
    shutil.copy2(LIB.parent / "epdiy/src/epd_metrics.h", root / "epd_metrics.h")
    exe = root / "epdiy"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-pthread",
                    "-Wall", "-Wextra", "-Wno-unused-parameter", "-DFREEINK_DRIVER_EPDIY=1",
                    "-DFREEINK_EPDIY_CONFIG=testConfig", "-I"+str(root), "-I"+str(root / "include"),
                    str(HERE / "test_epdiy.cpp"), str(LIB.parent / "epdiy/src/epd_metrics.c"), str(root / "src/driver/EpdiyDriver.cpp"),
                    "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=15)
