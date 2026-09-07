; YuyinIme one-click setup
Unicode true
Name "译语输入法 YuyinIme"
OutFile "..\dist\YuyinIme-Setup.exe"
InstallDir "$LOCALAPPDATA\YuyinIme"
RequestExecutionLevel user
Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles
Section "install"
  nsExec::Exec 'taskkill /f /im YuyinIme.exe'
  Sleep 300
  SetOutPath "$INSTDIR"
  File "..\ime\YuyinIme.exe"
  File "..\ime\dict.tsv"
  File /oname=README.txt "..\README.txt"
  SetOutPath "$INSTDIR\packs"
  File "..\packs\*.tsv"
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\译语输入法"
  CreateShortcut "$SMPROGRAMS\译语输入法\译语输入法.lnk" "$INSTDIR\YuyinIme.exe"
  CreateShortcut "$SMPROGRAMS\译语输入法\卸载译语输入法.lnk" "$INSTDIR\Uninstall.exe"
  CreateShortcut "$DESKTOP\译语输入法.lnk" "$INSTDIR\YuyinIme.exe"
  WriteRegStr HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" "YuyinIme" "$INSTDIR\YuyinIme.exe"
  WriteRegStr HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\YuyinIme" "DisplayName" "译语输入法 YuyinIme"
  WriteRegStr HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\YuyinIme" "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\YuyinIme" "DisplayIcon" "$INSTDIR\YuyinIme.exe"
  WriteRegStr HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\YuyinIme" "DisplayVersion" "1.0.0"
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  Exec "$INSTDIR\YuyinIme.exe"
SectionEnd
Section "un.install"
  nsExec::Exec 'taskkill /f /im YuyinIme.exe'
  DeleteRegValue HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" "YuyinIme"
  DeleteRegKey HKCU "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\YuyinIme"
  Delete "$SMPROGRAMS\译语输入法\译语输入法.lnk"
  Delete "$SMPROGRAMS\译语输入法\卸载译语输入法.lnk"
  RMDir "$SMPROGRAMS\译语输入法"
  Delete "$DESKTOP\译语输入法.lnk"
  Delete "$INSTDIR\YuyinIme.exe"
  Delete "$INSTDIR\dict.tsv"
  Delete "$INSTDIR\README.txt"
  Delete "$INSTDIR\packs\*.tsv"
  RMDir "$INSTDIR\packs"
  Delete "$INSTDIR\Uninstall.exe"
  MessageBox MB_YESNO|MB_ICONQUESTION "同时删除学习数据，词频与联想记忱？" IDYES del_user IDNO no_del
del_user:
    Delete "$INSTDIR\freq.tsv"
    Delete "$INSTDIR\bigram.tsv"
no_del:
  RMDir "$INSTDIR"
SectionEnd
