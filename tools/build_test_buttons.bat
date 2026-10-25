@echo off
rem build_test_buttons.bat - the buttons file, offline (src\ut_buttons.h): the grammar and every
rem refusal, buttons -> groups, [unassigned] and its rewrite, the default file against the Python
rem twin's fixture (data\oracle\uniquetab-buttons.ini), the reserved ids and tags.
rem NO GAME NEEDED; writes only under build\test.
rem   tools\build_test_buttons.bat        (finds vcvars32 itself, like build.bat: x86, as the mod)
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_buttons.exe" ^
   "%ROOT%\tools\test_buttons.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_buttons.exe" "%ROOT%" > "%OUT%\test_buttons.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_buttons.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_buttons.out.txt"
exit /b %RC%
