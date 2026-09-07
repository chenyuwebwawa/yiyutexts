@echo off
REM 译语输入法 卸载（绿色版 / 安装版通用）
echo 正在结束输入法进程...
taskkill /f /im YuyinIme.exe >nul 2>&1

echo 正在移除开机自启...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v YuyinIme >nul 2>&1

echo 正在删除快捷方式...
del "%USERPROFILE%\Desktop\译语输入法.lnk" >nul 2>&1
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\译语输入法\译语输入法.lnk" >nul 2>&1
del "%APPDATA%\Microsoft\Windows\Start Menu\Programs\译语输入法\卸载译语输入法.lnk" >nul 2>&1
rd "%APPDATA%\Microsoft\Windows\Start Menu\Programs\译语输入法" >nul 2>&1

echo 正在删除安装版目录（如存在）...
rd /s /q "%LOCALAPPDATA%\YuyinIme" >nul 2>&1

echo 正在清理旧版 TSF 注册残留（如存在）...
reg delete "HKLM\SOFTWARE\Microsoft\CTF\TIP\{A7E5A7C1-6E2B-4A0C-9D8F-1B2C3D4E5F60}" /f >nul 2>&1
reg delete "HKCU\Software\Microsoft\CTF\SortOrder\AssemblyItem" /f >nul 2>&1

echo.
echo 卸载完成。如果 Win+空格 列表里还有旧的“译语输入法”幽灵项，注销重新登录一次就会消失。
pause
