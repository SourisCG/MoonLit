; MoonLit Windows installer (NSIS 3).
; Per-user install: no elevation required. User data (clips, database,
; configuration) lives outside the install directory and is preserved on
; upgrade and uninstall.
;
; Build: makensis /DVERSION=1.0.0 /DSTAGING=<dir> moonlit.nsi

Unicode true
RequestExecutionLevel user
SetCompressor /SOLID lzma

!ifndef MOONLIT_ICON
  ; Relative to this script (cmake/windows/moonlit.nsi).
  !define MOONLIT_ICON "..\..\frontend\cmake\windows\MoonLit.ico"
!endif

; MoonLit product icons (crescent moon + play).
Icon "${MOONLIT_ICON}"
UninstallIcon "${MOONLIT_ICON}"

!ifndef VERSION
  !define VERSION "1.0.0"
!endif
!ifndef STAGING
  !error "STAGING directory must be defined"
!endif
!ifndef APPID
  !define APPID "MoonLit"
!endif

Name "MoonLit ${VERSION}"
OutFile "MoonLit-${VERSION}-Setup.exe"
; Installs to a dedicated program directory. The user data root
; (%LOCALAPPDATA%\MoonLit for clips/database, %APPDATA%\MoonLit for config)
; is a DIFFERENT directory and is preserved on upgrade and uninstall.
InstallDir "$LOCALAPPDATA\Programs\MoonLit"
InstallDirRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "InstallLocation"
ShowInstDetails show
ShowUninstDetails show

Page directory
Page instfiles
UninstPage uninstConfirm
UninstPage instfiles

Section "MoonLit" SEC_APP
  SetOutPath "$INSTDIR"
  File /r /x *.pdb /x SHA256SUMS.txt "${STAGING}\*"

  WriteUninstaller "$INSTDIR\Uninstall MoonLit.exe"

  CreateDirectory "$SMPROGRAMS\MoonLit"
  CreateShortcut "$SMPROGRAMS\MoonLit\MoonLit.lnk" "$INSTDIR\bin\64bit\MoonLit.exe"
  CreateShortcut "$SMPROGRAMS\MoonLit\Uninstall MoonLit.lnk" "$INSTDIR\Uninstall MoonLit.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "DisplayName" "MoonLit ${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "Publisher" "MoonLit"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "DisplayIcon" "$INSTDIR\bin\64bit\MoonLit.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "UninstallString" '"$INSTDIR\Uninstall MoonLit.exe"'
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit" "NoRepair" 1
SectionEnd

Section "Uninstall"
  ; User clips, database and configuration live outside the install directory
  ; (%LOCALAPPDATA%\MoonLit and %APPDATA%\MoonLit) and are intentionally kept.
  Delete "$INSTDIR\Uninstall MoonLit.exe"
  RMDir /r "$INSTDIR"
  Delete "$SMPROGRAMS\MoonLit\MoonLit.lnk"
  Delete "$SMPROGRAMS\MoonLit\Uninstall MoonLit.lnk"
  RMDir "$SMPROGRAMS\MoonLit"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\MoonLit"
SectionEnd
