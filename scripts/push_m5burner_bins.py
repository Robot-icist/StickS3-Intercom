import os
import sys
import glob
import shutil
import subprocess

try:
    ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
except NameError:
    ROOT = os.getcwd()

MAPPING = {
    "sticks3_ble_hid": "firmware_sticks3_ble_hid",
    "sticks3_intercom": "firmware_sticks3_intercom",
}

FILES = {
    "bootloader.bin": "bootloader_0x0.bin",
    "partitions.bin": "partitions_0x8000.bin",
    "firmware.bin": "firmware_0x10000.bin",
    "littlefs.bin": "littlefs_0x670000.bin",
}

BOOT_APP0_CANDIDATES = glob.glob(os.path.join(os.path.expanduser("~"), ".platformio", "packages", "framework-arduinoespressif32*", "tools", "partitions", "boot_app0.bin"))


def find_boot_app0():
    for candidate in BOOT_APP0_CANDIDATES:
        if os.path.exists(candidate):
            return candidate
    return None


for env_name, folder in MAPPING.items():
    build_dir = os.path.join(ROOT, ".pio", "build", env_name)
    if not os.path.isdir(build_dir):
        continue
    littlefs = os.path.join(build_dir, "littlefs.bin")
    if not os.path.exists(littlefs):
        pio = shutil.which("pio")
        if pio:
            subprocess.run([pio, "run", "-e", env_name, "-t", "buildfs"], cwd=ROOT, check=True)
    dest_dir = os.path.join(ROOT, folder)
    os.makedirs(dest_dir, exist_ok=True)
    for src_name, dst_name in FILES.items():
        src = os.path.join(build_dir, src_name)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(dest_dir, dst_name))
            print(f"[m5burner] {env_name}: copied {src_name} -> {folder}/{dst_name}")

    # boot_app0.bin is provided by the Arduino-ESP32 framework (not the build dir)
    boot_app0_src = find_boot_app0()
    if boot_app0_src:
        shutil.copy2(boot_app0_src, os.path.join(dest_dir, "boot_app0_0xe000.bin"))
        print(f"[m5burner] {env_name}: copied boot_app0.bin -> {folder}/boot_app0_0xe000.bin")

    # Build a single merged flash image for M5Burner web uploads (one .bin at 0x0)
    bootloader = os.path.join(dest_dir, "bootloader_0x0.bin")
    partitions = os.path.join(dest_dir, "partitions_0x8000.bin")
    boot_app0 = os.path.join(dest_dir, "boot_app0_0xe000.bin")
    firmware = os.path.join(dest_dir, "firmware_0x10000.bin")
    littlefs_dst = os.path.join(dest_dir, "littlefs_0x670000.bin")
    merged = os.path.join(dest_dir, "full_flash_0x0.bin")
    required = [bootloader, partitions, boot_app0, firmware, littlefs_dst]
    if all(os.path.exists(p) for p in required):
        cmd = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin",
               "--pad-to-size", "8MB", "-o", merged,
               "0x0", bootloader,
               "0x8000", partitions,
               "0xe000", boot_app0,
               "0x10000", firmware,
               "0x670000", littlefs_dst]
        subprocess.run(cmd, check=True)
        print(f"[m5burner] {env_name}: merged full flash image -> {folder}/full_flash_0x0.bin")
    else:
        print(f"[m5burner] {env_name}: skipped merge (missing inputs)")
