#!/usr/bin/env python3
"""编译 ime/YuyinIme.cpp → ime/YuyinIme.exe（自动定位 MSVC，兼容所有 VS2022 版本）"""
import glob, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(ROOT, "ime"))

candidates = (
    glob.glob(r"C:\Program Files (x86)\Microsoft Visual Studio\2022\*\VC\Tools\MSVC")
    + glob.glob(r"C:\Program Files\Microsoft Visual Studio\2022\*\VC\Tools\MSVC")
)
if not candidates:
    sys.exit("未找到 MSVC，请安装 VS2022 Build Tools（C++ 工作负载）")
root = sorted(candidates)[-1]          # 任一版本均可，取最后一个
ver = os.listdir(root)[0]
bindir = os.path.join(root, ver, "bin", "Hostx64", "x64")

sdk_inc = r"C:\Program Files (x86)\Windows Kits\10\Include"
sdks = sorted(os.listdir(sdk_inc)) if os.path.isdir(sdk_inc) else []
if not sdks:
    sys.exit("未找到 Windows SDK")
sdkv = sdks[-1]
sdk_lib = os.path.join(r"C:\Program Files (x86)\Windows Kits\10\Lib", sdkv)

env = dict(os.environ)
env["PATH"] = bindir + os.pathsep + env.get("PATH", "")
env["INCLUDE"] = os.pathsep.join([
    os.path.join(root, ver, "include"),
    os.path.join(sdk_inc, sdkv, "ucrt"),
    os.path.join(sdk_inc, sdkv, "um"),
    os.path.join(sdk_inc, sdkv, "shared"),
])
env["LIB"] = os.pathsep.join([
    os.path.join(root, ver, "lib", "x64"),
    os.path.join(sdk_lib, "ucrt", "x64"),
    os.path.join(sdk_lib, "um", "x64"),
])

r = subprocess.run([os.path.join(bindir, "cl.exe"),
    "/nologo", "/utf-8", "/O2", "/W3", "YuyinIme.cpp",
    "/FeYuyinIme.exe", "/FoYuyinIme.obj",
    "/link", "user32.lib", "gdi32.lib", "advapi32.lib", "shell32.lib"],
    env=env, capture_output=True, cwd=os.getcwd())
out = (r.stdout or b"").decode("gbk", "replace")
errs = [ln for ln in out.split("\n") if "error" in ln.lower()]
print("\n".join(errs[:6]) if errs else "compile OK")
sys.exit(r.returncode)
