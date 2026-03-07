!include "MUI2.nsh"

; General Configuration
Name "SavvyCAN"
OutFile "SavvyCAN_Setup_v1.0.exe"
InstallDir "$PROGRAMFILES64\SavvyCAN"
RequestExecutionLevel admin

; UI Settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; -------------------------------------------------------------------
; INSTALLER SECTION
; -------------------------------------------------------------------
Section "Install"
    ; Set the installation directory
    SetOutPath "$INSTDIR"

    ; Grab EVERYTHING from our deployment folder
    File /r "SavvyCAN_Ready_For_Windows\*"

    ; Create the uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Create Start Menu Shortcuts
    CreateDirectory "$SMPROGRAMS\SavvyCAN"
    CreateShortcut "$SMPROGRAMS\SavvyCAN\SavvyCAN.lnk" "$INSTDIR\SavvyCAN.exe"
    CreateShortcut "$SMPROGRAMS\SavvyCAN\Uninstall SavvyCAN.lnk" "$INSTDIR\Uninstall.exe"

    ; Create Desktop Shortcut
    CreateShortcut "$DESKTOP\SavvyCAN.lnk" "$INSTDIR\SavvyCAN.exe"
SectionEnd

; -------------------------------------------------------------------
; UNINSTALLER SECTION
; -------------------------------------------------------------------
Section "Uninstall"
    ; Remove files and directories
    RMDir /r "$INSTDIR"

    ; Remove shortcuts
    Delete "$SMPROGRAMS\SavvyCAN\SavvyCAN.lnk"
    Delete "$SMPROGRAMS\SavvyCAN\Uninstall SavvyCAN.lnk"
    RMDir "$SMPROGRAMS\SavvyCAN"
    Delete "$DESKTOP\SavvyCAN.lnk"
SectionEnd
