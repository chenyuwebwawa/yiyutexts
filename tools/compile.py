#!/usr/bin/env python3
"""编译 ime/YuyinIme.cpp → ime/YuyinIme.exe（直接调用 MSVC，绕过 VsDevCmd 环境问题）"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.join(ROOT, "ime"))

root = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC"
if not os.path.isdir(root):
    root = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC"
ver = os.listdir(root)[0]
bindir = os.path.join(root, ver, "bin", "Hostx64", "x64")
sdk_inc = r"C:\Program Files (x86)\Windows Kits\10\Include"
sdkv = os.listdir(sdk_inc)[0]
sdk_lib = r"C:\Program Files (x86)\Windows Kits\10\Lib" + chr(92) + sdkv

env = dict(os.environ)
env["PATH"] = bindir + ";" + env.get("PATH", "")
env["INCLUDE"] = ";".join([os.path.join(root, ver, "include"),
    os.path.join(sdk_inc, sdkv, "ucrt"), os.path.join(sdk_inc, sdkv, "um"),
    os.path.join(sdk_inc, sdkv, "shared")])
env["LIB"] = ";".join([os.path.join(root, ver, "lib", "x64"),
    os.path.join(sdk_lib, "ucrt", "x64"), os.path.join(sdk_lib, "um", "x64")])

r = subprocess.run([os.path.join(bindir, "cl.exe"),
    "/nologo", "/utf-8", "/O2", "/W3", "YuyinIme.cpp",
    "/FeYuyinIme.exe", "/FoYuyinIme.obj",
    "/link", "user32.lib", "gdi32.lib", "advapi32.lib", "shell32.lib"],
    env=env, capture_output=True, cwd=os.getcwd())
out = (r.stdout or b"").decode("gbk", "replace")
errs = [l for l in out.split("\n") if "error" in l.lower()]
print("\n".join(errs[:6]) if errs else "compile OK")
sys.exit(r.returncode)
