@echo off
rem build_test_journalguard.bat - the uninstall guard (tools\journal_guard.ps1) and undeploy.bat's
rem purge, offline, over folders it makes under build\test\journalguard. NO GAME NEEDED.
rem   tools\build_test_journalguard.bat
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

rem The transcript is kept, so a failure can be read afterwards instead of re-run.
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\test_journal_guard.ps1" > "%OUT%\test_journal_guard.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_journal_guard.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_journal_guard.out.txt"
exit /b %RC%
