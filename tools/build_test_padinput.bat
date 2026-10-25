@echo off
rem build_test_padinput.bat - the screen pad's press rules, offline. NO GAME NEEDED.
rem   tools\build_test_padinput.bat        (finds vcvars32 itself, like build.bat)
rem
rem It compiles the REAL header the mod compiles - src\ut_padinput.h - so every rewrite, press and
rem hand-over it proves is what the GetMouseEvent detour runs, over the engine's 0x28-byte record.
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
   /Fo"%OUT%\\" /Fe"%OUT%\test_padinput.exe" "%ROOT%\tools\test_padinput.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_padinput.exe" "%ROOT%" > "%OUT%\test_padinput.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_padinput.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_padinput.out.txt"
exit /b %RC%
