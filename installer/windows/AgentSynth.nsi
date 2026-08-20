; AgentSynth Windows installer (P5-6). Per-user install (no admin/UAC), built so WinSparkle's
; default "run the downloaded file" auto-update flow (see docs/distribution.md) has a real
; installer to run instead of a bare portable exe.
;
; Invoked from CI as:
;   makensis /DVERSION=<PROJECT_VERSION> /DSTAGE_DIR=<dir containing "Agent Synth.exe" + WinSparkle.dll> installer\windows\AgentSynth.nsi
; Output filename is pinned to AgentSynthSetup.exe — the marketing site's download page
; (synth-platform/apps/web) references this exact name; don't rename without updating both.

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef STAGE_DIR
  !define STAGE_DIR "..\..\build\AgentSynth_artefacts\Release"
!endif

!include "MUI2.nsh"

Name "Agent Synth"
; Relative to makensis's invocation cwd (CI runs it from the repo root — see
; build-artifacts.yml's "Build Windows installer" step), NOT to this script's own directory.
OutFile "installer\windows\AgentSynthSetup.exe"
Unicode True

; Per-user install, HKCU only — no admin elevation required. This keeps WinSparkle's silent-ish
; update flow (it already shows its own "update available" dialog before running this installer,
; so a UAC prompt wouldn't break anything, but per-user avoids it entirely) friction-free, and
; matches "no code-signing cert yet, no admin story" positioning (see docs/distribution.md).
InstallDir "$LOCALAPPDATA\AgentSynth"
RequestExecutionLevel user

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\Agent Synth.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run Agent Synth"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
  SetShellVarContext current
  SetOutPath "$INSTDIR"

  ; Upgrade-in-place: WinSparkle has already asked the running app to quit (see
  ; WinSparkleUpdateManager.cpp's shutdown-request callback) by the time this installer runs, so
  ; overwriting files here doesn't hit a file lock.
  File "${STAGE_DIR}\Agent Synth.exe"
  File "${STAGE_DIR}\WinSparkle.dll"

  WriteUninstaller "$INSTDIR\Uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "DisplayName" "Agent Synth"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "UninstallString" "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "DisplayVersion" "${VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "Publisher" "Agent Synth"
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth" \
    "NoRepair" 1

  CreateDirectory "$SMPROGRAMS\Agent Synth"
  CreateShortcut "$SMPROGRAMS\Agent Synth\Agent Synth.lnk" "$INSTDIR\Agent Synth.exe"
  CreateShortcut "$SMPROGRAMS\Agent Synth\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
  SetShellVarContext current

  Delete "$INSTDIR\Agent Synth.exe"
  Delete "$INSTDIR\WinSparkle.dll"
  Delete "$INSTDIR\Uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\Agent Synth\Agent Synth.lnk"
  Delete "$SMPROGRAMS\Agent Synth\Uninstall.lnk"
  RMDir "$SMPROGRAMS\Agent Synth"

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\AgentSynth"
SectionEnd
