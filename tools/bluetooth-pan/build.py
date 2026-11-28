#!/usr/bin/env python3
"""Build the ESP32-S31 Arduino 4.0 RC1 libraries with the official PAN backport.

All package changes stay inside --core-dir. No system PlatformIO packages change.
Run from the firmware project; allow several GB and a first-time network download.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import venv

from source import IDF_COMMIT, hydrate, idf_source, verify_submodules

HERE = Path(__file__).resolve().parent
PLATFORM = "https://github.com/pioarduino/platform-espressif32/releases/download/61.04.00-RC1/platform-espressif32.zip"
CORE = "ba0ce19fe387aef748db7475e514278d621a6586"
BUILDER = "8343d0aa1995f1d23d5dbe0b03e66ee3f242cd15"
TINYUSB = "b80f1c107d0a33eb3be055f95fe0b3b9d6c0be48"
PACKAGES = [
    "https://github.com/espressif/arduino-esp32/releases/download/4.0.0-RC1/esp32-core-4.0.0-RC1.tar.xz",
    "https://github.com/espressif/arduino-esp32/releases/download/4.0.0-RC1/esp32-core-4.0.0-RC1-libs.tar.xz",
    "https://github.com/pioarduino/esp-idf/releases/download/v6.1.0/esp-idf-v6.1.0.tar.xz",
]


def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


def download(url, path):
    if path.exists():
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".part")
    run(["curl", "--fail", "--location", "--retry", "3", "--max-time", "300",
         "--output", temporary, url])
    temporary.replace(path)


def export_libraries(core, output):
    if output.exists() and not (output / "freeink-pan.json").exists():
        raise RuntimeError("Output exists without FreeInk PAN metadata; select an empty output directory")
    packages = core / "packages"
    source = packages / "framework-arduinoespressif32-libs"
    idf = packages / "framework-espidf"
    # An export-only run must not label old archives with the new patch hash.
    nm = packages / "toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"
    symbols = subprocess.check_output([str(nm), "-A", str(source / "esp32s31/lib/libmbedtls.a")],text=True)
    bundle = [line for line in symbols.splitlines() if ":esp_crt_bundle.c.o:" in line]
    if (not all(any(line.endswith(" U " + name) for line in bundle)
                for name in ("mbedtls_calloc", "mbedtls_free")) or
            any(line.endswith(" U calloc") for line in bundle)):
        raise RuntimeError("Certificate bundle allocator patch is absent from compiled libraries")
    # Export to a staging directory: an interrupted build cannot replace a good package.
    stage = output.with_name(output.name + ".building")
    if stage.exists():
        shutil.rmtree(stage)
    shutil.copytree(source, stage)
    (stage / "sdkconfig").unlink(missing_ok=True)  # PIO hybrid-build trigger, not the chip config.
    chip = stage / "esp32s31"
    header = "bt/host/bluedroid/api/include/api"
    shutil.copytree(idf / "components" / header, chip / "include" / header, dirs_exist_ok=True)
    controller = idf / "components/bt/controller/lib_esp32s31/esp32s31-bt-lib"
    for archive in ("libbredr_app.a", "libble_app.a", "libbtdm_common.a"):
        shutil.copy2(controller / archive, chip / "lib" / archive)
    script = chip / "pioarduino-build.py"
    text = script.read_text()
    # The hybrid exporter may already contain the Classic archive. Retain both
    # controller halves for BTDM; never convert BLE linkage into Classic-only.
    if '"-lbredr_app"' not in text:
        text = text.replace('"-lble_app"', '"-lble_app", "-lbredr_app"')
    if '"-lble_app"' not in text:
        text = text.replace('"-lbredr_app"', '"-lbredr_app", "-lble_app"')
    text = text.replace('        "-Wl,--wrap=log_printf",\n', '')
    text += '\n# Export the enabled Classic Bluetooth public API.\n'
    text += f'env.AppendUnique(CPPPATH=[join(FRAMEWORK_SDK_DIR, "esp32s31", "include", "{header}")])\n'
    script.write_text(text)
    config = (chip / "qio_opi/include/sdkconfig.h").read_text()
    for symbol in ("BT_BLUEDROID_ENABLED", "BT_CLASSIC_ENABLED", "BT_PAN_ENABLED", "BT_BLE_ENABLED", "BTDM_CTRL_MODE_BTDM", "BT_GATTC_ENABLE", "TINYUSB_MSC_ENABLED"):
        if f"#define CONFIG_{symbol} 1" not in config:
            raise RuntimeError(f"Missing CONFIG_{symbol}; refusing to export incomplete libraries")
    for symbol in ("TINYUSB_HOST_ENABLED", "TINYUSB_HID_ENABLED", "TINYUSB_MIDI_ENABLED",
                   "TINYUSB_AUDIO_ENABLED", "TINYUSB_VIDEO_ENABLED", "TINYUSB_DFU_ENABLED",
                   "TINYUSB_DFU_RT_ENABLED", "TINYUSB_VENDOR_ENABLED", "TINYUSB_NCM_ENABLED",
                   "BT_CTRL_ALLOW_MALLOC_FROM_SPIRAM"):
        if f"#define CONFIG_{symbol} 1" in config:
            raise RuntimeError(f"Unexpected CONFIG_{symbol}; resource profile does not match")
    for symbol, value in {"BT_LE_MAX_CONNECTIONS": 1, "BT_CTRL_BR_EDR_MAX_ACL_CONN": 1,
                          "TINYUSB_CDC_MAX_PORTS": 1, "BT_LE_MSYS_1_BLOCK_COUNT": 6,
                          "BT_LE_MSYS_2_BLOCK_COUNT": 12, "BT_LE_ACL_BUF_COUNT": 8,
                          "BT_LE_HCI_EVT_HI_BUF_COUNT": 12, "BT_LE_HCI_EVT_LO_BUF_COUNT": 4}.items():
        if f"#define CONFIG_{symbol} {value}\n" not in config:
            raise RuntimeError(f"Unexpected CONFIG_{symbol}; resource profile does not match")
    metadata = json.loads((stage / "package.json").read_text())
    metadata["version"] = "6.1.0+freeink.pan.hid.3"
    (stage / "package.json").write_text(json.dumps(metadata, indent=2) + "\n")
    (stage / "freeink-pan.json").write_text(json.dumps({
        "idf_base": IDF_COMMIT, "pan_source": "ad20ded62a2d67a26e501fdc1905f43ba7ad6c81",
        "patch_sha256": hashlib.sha256((HERE / "esp-idf-pan.patch").read_bytes()).hexdigest(),
        "tls_allocator_patch_sha256": hashlib.sha256((HERE / "esp-idf-tls-allocator.patch").read_bytes()).hexdigest(),
        "config_sha256": hashlib.sha256((HERE / "pan.sdkconfig").read_bytes()).hexdigest(),
        "submodules_sha256": hashlib.sha256((HERE / "submodules.json").read_bytes()).hexdigest(),
    }, indent=2) + "\n")
    backup = output.with_name(output.name + ".previous")
    if backup.exists():
        shutil.rmtree(backup)
    if output.exists():
        output.rename(backup)
    stage.rename(output)
    if backup.exists():
        shutil.rmtree(backup)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core-dir", type=Path, default=Path(".tools/pan-platformio"))
    parser.add_argument("--work-dir", type=Path, default=Path(".tools/pan-build"))
    parser.add_argument("--output", type=Path, default=Path(".tools/bluetooth-pan-libs"))
    parser.add_argument("--export-only", action="store_true", help="Re-export an already compiled isolated package")
    args = parser.parse_args()
    core, work, output = (p.resolve() for p in (args.core_dir, args.work_dir, args.output))
    if core == output or core in output.parents or work in output.parents:
        parser.error("Output must be separate from the package and build directories")
    if args.export_only:
        export_libraries(core, output)
        return
    work.mkdir(parents=True, exist_ok=True)
    cache = work / "downloads"
    python = core / "penv/bin/python"
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(python.parent.parent)
    version = subprocess.run([str(python), "-c", "import platformio; print(platformio.__version__)"],
                             capture_output=True, text=True)
    # Pioarduino 6.2.0.post1 does not satisfy this platform's >=6.2.0 check.
    if version.returncode or version.stdout.strip() != "6.2.0":
        archive = cache / "platformio-core.tar.gz"
        download(f"https://api.github.com/repos/pioarduino/platformio-core/tarball/{CORE}", archive)
        run([python, "-m", "pip", "install", archive])
    env = dict(os.environ, PLATFORMIO_CORE_DIR=str(core))
    pio = [python, "-m", "platformio"]
    run([*pio, "pkg", "install", "--global", "--platform", PLATFORM], env=env)
    for package in PACKAGES:
        run([*pio, "pkg", "install", "--global", "--tool", package], env=env)
    idf = core / "packages/framework-espidf"
    tree = hydrate(idf, cache / "idf-exact", "espressif/esp-idf", IDF_COMMIT, idf_source)
    # Verification removes generated files; PIO recreates this component only
    # when its directory is absent, including on subsequent builds.
    shutil.rmtree(idf / "components/__pio_env", ignore_errors=True)
    submodules = json.loads((HERE / "submodules.json").read_text())
    verify_submodules(tree, submodules)
    for item in submodules:
        include = (lambda path: path.startswith("esp32s31") or "/" not in path) if item["path"] in (
            "components/esp_wifi/lib", "components/esp_phy/lib", "components/esp_coex/lib") else (lambda path: True)
        hydrate(idf / item["path"], cache / "submodules" / item["path"].replace("/", "_"),
                item["repository"], item["sha"], include)
    patches = [HERE / "esp-idf-pan.patch", HERE / "esp-idf-tls-allocator.patch"]
    # Check both directions so rerunning with the same input is harmless. Fail
    # on a different baseline instead of silently producing a mixed framework.
    patch_env = dict(os.environ, GIT_CEILING_DIRECTORIES=str(idf.parent))
    for patch in patches:
        check = subprocess.run(["git", "apply", "--check", str(patch)], cwd=idf, env=patch_env, capture_output=True)
        if check.returncode == 0:
            run(["git", "apply", str(patch)], cwd=idf, env=patch_env)
        else:
            run(["git", "apply", "--reverse", "--check", str(patch)], cwd=idf, env=patch_env)
    run([sys.executable, HERE / "test_tls_allocator.py", "--idf", idf])
    component = work / "components/arduino_tinyusb"
    for filename in ("CMakeLists.txt", "Kconfig.projbuild", "include/tusb_config.h"):
        download(f"https://raw.githubusercontent.com/espressif/esp32-arduino-lib-builder/{BUILDER}/components/arduino_tinyusb/{filename}", component / filename)
    # The upstream builder enables the entire USB host stack whenever device
    # mode is enabled. Give the product an explicit, reproducible host opt-in.
    kconfig = component / "Kconfig.projbuild"
    configuration = kconfig.read_text()
    if "config TINYUSB_HOST_ENABLED" not in configuration:
        configuration = configuration.replace('    menu "Serial (CDC) driver"',
            '    config TINYUSB_HOST_ENABLED\n'
            '        bool "Enable TinyUSB host stack"\n'
            '        default n\n'
            '        depends on TINYUSB_ENABLED\n\n'
            '    menu "Serial (CDC) driver"')
        kconfig.write_text(configuration)
    header = component / "include/tusb_config.h"
    configuration = header.read_text().replace(
        '#define CFG_TUH_ENABLED             CFG_TUD_ENABLED',
        '#ifdef CONFIG_TINYUSB_HOST_ENABLED\n'
        '#define CFG_TUH_ENABLED             CONFIG_TINYUSB_HOST_ENABLED\n'
        '#else\n#define CFG_TUH_ENABLED             0\n#endif')
    if 'FreeInk device-only host classes' not in configuration:
        configuration += '\n// FreeInk device-only host classes (also used by Arduino wrappers).\n'
        configuration += '#if !CFG_TUH_ENABLED\n'
        for name in ('HUB', 'CDC', 'CDC_FTDI', 'CDC_CP210X', 'CDC_CH34X', 'HID', 'MSC', 'MIDI'):
            configuration += f'#undef CFG_TUH_{name}\n#define CFG_TUH_{name} 0\n'
        configuration += '#endif\n'
    header.write_text(configuration)
    tinyusb = cache / "tinyusb.tar.gz"
    if not (component / "tinyusb/src/tusb.c").exists():
        download(f"https://api.github.com/repos/hathach/tinyusb/tarball/{TINYUSB}", tinyusb)
        with tarfile.open(tinyusb) as archive:
            # GitHub archives have a single enclosing directory.
            archive.extractall(cache / "tinyusb", filter="data")
        extracted = next((cache / "tinyusb").iterdir())
        shutil.move(str(extracted), component / "tinyusb")
    # Keep the resolved dependency lock, which the hybrid exporter otherwise deletes.
    builder = core / "platforms/espressif32/builder/frameworks/espidf.py"
    script = builder.read_text().replace('os.remove(str(Path(env.subst("$PROJECT_DIR")) / "dependencies.lock"))', 'pass # Preserve dependency lock for reproducible PAN libraries')
    builder.write_text(script)
    lock = HERE / "dependencies.lock"
    if lock.exists():
        shutil.copy2(lock, work / "dependencies.lock")
    patch_hash = hashlib.sha256(b''.join(p.read_bytes() for p in patches)).hexdigest()
    (work / "pan.sdkconfig").write_text(f"# FreeInk IDF {IDF_COMMIT} PAN {patch_hash}\n" + (HERE / "pan.sdkconfig").read_text())
    (work / "platformio.ini").write_text(f"""[env:pan]
platform = {PLATFORM}
board = esp32-s31-coreboard-1
framework = arduino
board_build.flash_mode = qio
board_build.flash_size = 16MB
custom_sdkconfig = file://pan.sdkconfig
""")
    (work / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.16)\ninclude($ENV{IDF_PATH}/tools/cmake/project.cmake)\nproject(freeink_pan)\n')
    (work / "src").mkdir(exist_ok=True)
    (work / "src/main.cpp").write_text('#include <Arduino.h>\nvoid setup() {}\nvoid loop() { delay(1000); }\n')
    run([*pio, "run", "--project-dir", work], env=env)
    export_libraries(core, output)
    # A separate Arduino link must pull in PAN, Classic controller and USB MSC.
    # The hybrid export's empty sketch would not detect missing archives/flags.
    smoke = work / "smoke"
    (smoke / "src").mkdir(parents=True, exist_ok=True)
    (smoke / "platformio.ini").write_text(f"""[env:pan]
platform = {PLATFORM}
board = esp32-s31-coreboard-1
framework = arduino
board_build.flash_mode = qio
platform_packages = framework-arduinoespressif32-libs @ symlink://{output}
build_flags = -DARDUINO_USB_MODE=1
""")
    shutil.copy2(HERE / "smoke.cpp", smoke / "src/main.cpp")
    run([*pio, "run", "--project-dir", smoke], env=env)
    print(f"PAN libraries and controller/USB link check passed: {output}")


if __name__ == "__main__":
    main()
