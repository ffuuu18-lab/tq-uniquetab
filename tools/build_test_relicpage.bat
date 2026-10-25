@echo off
rem build_test_relicpage.bat - the relic and charm page's layout (src\ut_relicpage.h), offline.
rem NO GAME NEEDED. Reads the oracle's uniq-relics.txt (the base game's list).
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
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_relicpage.exe" "%ROOT%\tools\test_relicpage.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_relicpage.exe" "%ROOT%\data\oracle\uniq-relics.txt" > "%OUT%\test_relicpage.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_relicpage.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_relicpage.out.txt"
exit /b %RC%
