import os
import shutil

Import("env")

project_dir = env.subst("$PROJECT_DIR")
src_dir = os.path.join(project_dir, "src", "controller", "data_ble")
data_dir = os.path.join(project_dir, "data")

if not os.path.isdir(src_dir):
    print("[copy_ble_web_data] Source not found:", src_dir)
    print("[copy_ble_web_data] Skipping copy")
else:
    os.makedirs(data_dir, exist_ok=True)
    for name in ("index.html", "app.js", "style.css", "manifest.json"):
        src_file = os.path.join(src_dir, name)
        if os.path.isfile(src_file):
            shutil.copy2(src_file, os.path.join(data_dir, name))
            print("[copy_ble_web_data] Copied:", name)
    print("[copy_ble_web_data] Done. Web data ready in:", data_dir)
