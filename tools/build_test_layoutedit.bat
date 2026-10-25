@echo off
rem build_test_layoutedit.bat - the in-game layout editor's pure half (src\ut_layoutedit.h), offline.
rem NO GAME NEEDED. The handles, the clamps of a drag, SAVE / CANCEL / RESET, the refusal with an item
rem on the cursor, the round trip of the lines SAVE writes.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /O2 /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_layoutedit.exe" "%ROOT%\tools\test_layoutedit.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_layoutedit.exe" "%ROOT%" > "%OUT%\test_layoutedit.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_layoutedit.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_layoutedit.out.txt"
exit /b %RC%
