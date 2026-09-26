@echo off
rem build_test_proto.bat - the prototypes' replica assembly (src\ut_proto.h), offline. NO GAME.
rem   tools\build_test_proto.bat           = --units (the replica bytes + every catalogue record)
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
   /Fo"%OUT%\\" /Fe"%OUT%\test_proto.exe" "%ROOT%\tools\test_proto.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

pushd "%ROOT%"
"%OUT%\test_proto.exe" --units > "%OUT%\test_proto.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
popd
type "%OUT%\test_proto.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_proto.out.txt"
exit /b %RC%
