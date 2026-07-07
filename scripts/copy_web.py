"""
copy_web.py - Pre-build script for PlatformIO
Copies web/ folder into data/ so it gets flashed to LittleFS
"""
import shutil, os

Import("env")

web_src  = os.path.join(env["PROJECT_DIR"], "web")
data_dst = os.path.join(env["PROJECT_DIR"], "data", "web")

def copy_web(source, target, env):
    if os.path.exists(data_dst):
        shutil.rmtree(data_dst)
    shutil.copytree(web_src, data_dst)
    print(f"[copy_web] Copied web/ → data/web/")

env.AddPreAction("uploadfs", copy_web)
env.AddPreAction("buildfs",  copy_web)
