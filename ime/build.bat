@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -no_logo
cl /nologo /utf-8 /O2 /W3 YuyinIme.cpp /FeYuyinIme.exe /FoYuyinIme.obj /link user32.lib gdi32.lib advapi32.lib shell32.lib
