; Game Transfer Hub installer.
; Build the Release configuration and create dist/ before invoking makensis.

!include "MUI2.nsh"
!include "x64.nsh"

!define APP_VERSION "6.9.0"

Name "Game Transfer Hub"
OutFile "dist\GameTransferHub-Setup-${APP_VERSION}.exe"
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
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "Game Transfer Hub"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "FileVersion" "${APP_VERSION}.0"
VIAddVersionKey "FileDescription" "Game Transfer Hub"
VIAddVersionKey "CompanyName" "Game Transfer Hub"
VIAddVersionKey "LegalCopyright" "2026"

; Section: Install
Section "Game Transfer Hub"
  SetOutPath "$INSTDIR"

  ; The application target is configured for static dependencies.
  File "build\src\app\Release\GameTransferHub.exe"

  ; Create program shortcuts
  SetOutPath "$INSTDIR"
  CreateDirectory "$SMPROGRAMS\Game Transfer Hub"
  CreateShortCut "$SMPROGRAMS\Game Transfer Hub\Game Transfer Hub.lnk" "$INSTDIR\GameTransferHub.exe"
  CreateShortCut "$SMPROGRAMS\Game Transfer Hub\Uninstall.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\Game Transfer Hub.lnk" "$INSTDIR\GameTransferHub.exe"

  ; Write registry keys for uninstall
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayName" "Game Transfer Hub"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "UninstallString" "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\GameTransferHub" "DisplayIcon" "$INSTDIR\GameTransferHub.exe"
  WriteRegStr HKLM "Software\GameTransferHub" "InstallLocation" "$INSTDIR"

  ; Create uninstaller
  WriteUninstaller "$INSTDIR\uninstall.exe"
SectionEnd

; Section: Uninstall
Section "Uninstall"
  ; Remove files
  Delete "$INSTDIR\GameTransferHub.exe"
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
