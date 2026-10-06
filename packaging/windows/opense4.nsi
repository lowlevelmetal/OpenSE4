; OpenSE4 installer for Windows (NSIS 3). tools/package_release.sh builds it from
; the staged Windows package:
;
;   makensis -DVERSION=0.2.1 -DVERSION_NUMBER=0.2.1 -DSTAGE=<staged folder>
;            -DOUTFILE=<setup.exe> packaging/windows/opense4.nsi
;
; It installs for all users into Program Files, adds OpenSE4 to the Start menu
; (and, if chosen, the desktop) and to Apps & features. Saved games and settings
; live in %APPDATA%\OpenSE4 and are left alone by the uninstaller.

Unicode true
ManifestDPIAware true
SetCompressor /SOLID lzma
RequestExecutionLevel admin

!macro REQUIRE_DEFINE name
    !ifndef ${name}
        !error "Define ${name} (see the top of this file)"
    !endif
!macroend
!insertmacro REQUIRE_DEFINE VERSION
!insertmacro REQUIRE_DEFINE VERSION_NUMBER
!insertmacro REQUIRE_DEFINE STAGE
!insertmacro REQUIRE_DEFINE OUTFILE

!define APP_NAME "OpenSE4"
!define PUBLISHER "The OpenSE4 developers"
!define HOMEPAGE "https://github.com/lowlevelmetal/OpenSE4"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\OpenSE4"
!define SETTINGS_KEY "Software\OpenSE4"

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"

Name "${APP_NAME}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\OpenSE4"
BrandingText "${APP_NAME} ${VERSION}"

VIProductVersion "${VERSION_NUMBER}.0"
VIFileVersion "${VERSION_NUMBER}.0"
VIAddVersionKey /LANG=1033 "ProductName" "${APP_NAME}"
VIAddVersionKey /LANG=1033 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "FileDescription" "${APP_NAME} installer"
VIAddVersionKey /LANG=1033 "CompanyName" "${PUBLISHER}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Free software under the GNU GPL 3.0 or later"

; --- pages ----------------------------------------------------------------------
!define MUI_ICON "opense4.ico"
!define MUI_UNICON "opense4.ico"
!define MUI_WELCOMEFINISHPAGE_BITMAP "installer-side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "installer-side.bmp"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

!define MUI_WELCOMEPAGE_TEXT "This will install ${APP_NAME} ${VERSION}, an open-source engine for Space Empires IV Deluxe.$\r$\n$\r$\n${APP_NAME} plays with your own installed copy of Space Empires IV Deluxe and reads its data, art and sound in place. Nothing from the original game is included, so install the game (for example from Steam) as well.$\r$\n$\r$\n${APP_NAME} is not affiliated with the game's publishers.$\r$\n$\r$\n$_CLICK"
!insertmacro MUI_PAGE_WELCOME

; The GPL needs no acceptance to use the program: the page only shows it.
!define MUI_LICENSEPAGE_TEXT_BOTTOM "${APP_NAME} is free software: you may use it for any purpose, and share and change it under these terms."
!define MUI_LICENSEPAGE_BUTTON "$(^NextBtn)"
!insertmacro MUI_PAGE_LICENSE "${STAGE}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

; Started through Explorer, so the game runs as the user, not as the elevated
; installer (its saves belong in the user's own %APPDATA%).
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APP_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION StartGame
!define MUI_FINISHPAGE_LINK "${APP_NAME} on GitHub"
!define MUI_FINISHPAGE_LINK_LOCATION "${HOMEPAGE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; --- install --------------------------------------------------------------------
Function StartGame
    Exec '"$WINDIR\explorer.exe" "$INSTDIR\opense4.exe"'
FunctionEnd

Function .onInit
    ${IfNot} ${RunningX64}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs a 64-bit version of Windows." /SD IDOK
        Abort
    ${EndIf}
    ; The programs run on Windows 7 with Service Pack 1 and every later Windows
    ; (docs/BUILDING.md, "Windows 7 to 11").
    ${IfNot} ${AtLeastWin7}
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs Windows 7 with Service Pack 1, or a later Windows." /SD IDOK
        Abort
    ${EndIf}
    ${If} ${IsWin7}
    ${AndIfNot} ${AtLeastServicePack} 1
        MessageBox MB_OK|MB_ICONSTOP "${APP_NAME} needs Service Pack 1 for Windows 7. Install it with Windows Update, then run this setup again." /SD IDOK
        Abort
    ${EndIf}
    SetRegView 64
    SetShellVarContext all
    ; An update goes where the last install went. InstallDirRegKey can't do this:
    ; it reads the 32-bit registry view, and the key is in the 64-bit one. An
    ; explicit /D= on the command line still wins.
    ReadRegStr $0 HKLM "${SETTINGS_KEY}" "InstallDir"
    ${If} $0 != ""
    ${AndIf} $INSTDIR == "$PROGRAMFILES64\OpenSE4"
        StrCpy $INSTDIR $0
    ${EndIf}
FunctionEnd

Section "${APP_NAME}" SecGame
    SectionIn RO
    SetOutPath "$INSTDIR"
    File "${STAGE}\opense4.exe"
    File "${STAGE}\opense4-server.exe"
    File "${STAGE}\opense4-datacheck.exe"
    File "${STAGE}\opense4-convert.exe"
    File "${STAGE}\opense4-sdk.exe"
    File "${STAGE}\README.md"
    File "${STAGE}\LICENSE"
    File "${STAGE}\THIRD_PARTY_NOTICES.txt"
    WriteUninstaller "$INSTDIR\uninstall.exe"

    CreateShortcut "$SMPROGRAMS\${APP_NAME}.lnk" "$INSTDIR\opense4.exe" "" "$INSTDIR\opense4.exe" 0

    ; The modding SDK's guide, reference and example mods, beside opense4-sdk.exe.
    SetOutPath "$INSTDIR\sdk"
    File /r "${STAGE}\sdk\*.*"
    SetOutPath "$INSTDIR"

    WriteRegStr HKLM "${SETTINGS_KEY}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "${APP_NAME}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\opense4.exe,0"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" "$\"$INSTDIR\uninstall.exe$\" /S"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "${HOMEPAGE}"
    WriteRegStr HKLM "${UNINSTALL_KEY}" "HelpLink" "${HOMEPAGE}"
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" "$0"
SectionEnd

Section "Desktop shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\opense4.exe" "" "$INSTDIR\opense4.exe" 0
SectionEnd

; --- uninstall ------------------------------------------------------------------
Function un.onInit
    SetRegView 64
    SetShellVarContext all
FunctionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\${APP_NAME}.lnk"
    Delete "$DESKTOP\${APP_NAME}.lnk"

    Delete "$INSTDIR\opense4.exe"
    Delete "$INSTDIR\opense4-server.exe"
    Delete "$INSTDIR\opense4-datacheck.exe"
    Delete "$INSTDIR\opense4-convert.exe"
    Delete "$INSTDIR\opense4-sdk.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\THIRD_PARTY_NOTICES.txt"
    RMDir /r "$INSTDIR\sdk"
    Delete "$INSTDIR\uninstall.exe"
    RMDir "$INSTDIR"

    DeleteRegKey HKLM "${UNINSTALL_KEY}"
    DeleteRegKey HKLM "${SETTINGS_KEY}"
SectionEnd
