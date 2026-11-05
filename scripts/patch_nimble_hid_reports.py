Import("env")

from pathlib import Path


platform = env.PioPlatform()
framework_dir = platform.get_package_dir("framework-arduinoespressif32")
service_source = Path(framework_dir) / "libraries" / "BLE" / "src" / "BLEService.cpp"
source = service_source.read_text(encoding="utf-8")

original = """if (pExisting != nullptr) {
    pExisting->m_removed = 0;
  } else"""
patched = """if (pExisting == pCharacteristic) {
    pExisting->m_removed = 0;
  } else"""

if patched in source:
    print("NimBLE HID duplicate-characteristic support is already patched.")
elif original in source:
    service_source.write_text(source.replace(original, patched, 1), encoding="utf-8")
    print("Patched NimBLE characteristic map for repeated HID report UUIDs.")
else:
    raise RuntimeError(
        "Unexpected BLEService.cpp contents; refusing to apply the "
        "NimBLE HID characteristic patch."
    )
