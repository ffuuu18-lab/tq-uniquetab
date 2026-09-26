@echo off
rem build_test_viewgate.bat - the view's ON/OFF state machine (src\ut_viewgate.h), offline.
rem NO GAME NEEDED. Every event order up to depth 7 against a simulated Transfer page.
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
   /Fo"%OUT%\\" /Fe"%OUT%\test_viewgate.exe" "%ROOT%\tools\test_viewgate.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_viewgate.exe" > "%OUT%\test_viewgate.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_viewgate.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_viewgate.out.txt"
exit /b %RC%
