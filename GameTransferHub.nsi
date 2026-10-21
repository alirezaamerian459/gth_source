; Game Transfer Hub Installer Script
; NSIS 3.0+ Required
; This script creates an Windows installer for Game Transfer Hub with .pkg recognition

!include "MUI2.nsh"
!include "x64.nsh"

; General settings
Name "Game Transfer Hub"
OutFile "GameTransferHub-Setup-Latest.exe"
InstallDir "$PROGRAMFILES\GameTransferHub"
InstallDirRegKey HKLM "Software\GameTransferHub" "InstallLocation"

; Require admin privileges
RequestExecutionLevel admin

; MUI Settings
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

; Version Info
VIProductVersion "6.9.0.0"
VIAddVersionKey "ProductName" "Game Transfer Hub"
VIAddVersionKey "ProductVersion" "6.9.0"
VIAddVersionKey "FileVersion" "6.9.0.0"
VIAddVersionKey "FileDescription" "Game Transfer Hub - PlayStation Game Manager with .pkg Recognition"
VIAddVersionKey "CompanyName" "Game Transfer Hub"
VIAddVersionKey "LegalCopyright" "2026"

; Section: Install
Section "Game Transfer Hub"
  SetOutPath "$INSTDIR"

  ; Copy main executable
  File "build\Release\GameTransferHubApp.exe"

  ; Copy required DLLs and dependencies
  File "build\_deps\curl-build\lib\Release\libcurl.dll"
  File "build\_deps\libssh2-build\src\Release\libssh2.dll"

  ; Create program shortcuts
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Game Transfer Hub"
  CreateShortCut "$SMPROGRAMS\Game Transfer Hub\Game Transfer Hub.lnk" "$INSTDIR\GameTransferHubApp.exe"
  CreateShortCut "$SMPROGRAMS\Game Transfer Hub\Uninstall.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\Game Transfer Hub.lnk" "$INSTDIR\GameTransferHubApp.exe"

  ; Write registry keys for uninstall
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayName" "Game Transfer Hub"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayVersion" "2.0.0"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayIcon" "$INSTDIR\GameTransferHubApp.exe"
  WriteRegStr HKLM "Software\GameTransferHub" "InstallLocation" "$INSTDIR"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Section: Uninstall
Section "Uninstall"
  ; Remove files
  Delete "$INSTDIR\GameTransferHubApp.exe"
  Delete "$INSTDIR\libcurl.dll"
  Delete "$INSTDIR\libssh2.dll"
  Delete "$INSTDIR\uninstall.exe"

  ; Remove shortcuts
  Delete "$SMPROGRAMS\Game Transfer Hub\Game Transfer Hub.lnk"
  Delete "$SMPROGRAMS\Game Transfer Hub\Uninstall.lnk"
  Delete "$DESKTOP\Game Transfer Hub.lnk"
  RMDir "$SMPROGRAMS\Game Transfer Hub"

  ; Remove directory
  RMDir "$INSTDIR"

  ; Remove registry keys
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub"
  DeleteRegKey HKLM "Software\GameTransferHub"
SectionEnd

; On init
Function .onInit
  ${If} ${RunningX64}
    ; 64-bit system - all good
  ${Else}
    MessageBox MB_ICONEXCLAMATION "This application requires Windows 64-bit (x64)"
    Abort
  ${EndIf}
FunctionEnd
