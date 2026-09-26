@echo off
rem build_test_store.bat - the private table (src\ut_store.cpp) and ut_depositgate.h, offline. NO GAME NEEDED.
rem TQ port of GD's tools\build_test_store.bat: x86, and the whole run stays inside
rem build\test\store-out (the test sets %UNIQUETAB_OUT% to it before any path call, and %UNIQUETAB_SAVEDATA% to a fake SaveData folder under it).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
set "JOUT=%OUT%\store-out"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%JOUT%" rmdir /s /q "%JOUT%"
mkdir "%JOUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I"%ROOT%\src" ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_store.exe" ^
   "%ROOT%\tools\test_store.cpp" "%ROOT%\src\ut_store.cpp" "%ROOT%\src\ut_rescue.cpp" "%ROOT%\src\ut_config.cpp" ^
   "%ROOT%\src\ut_log.cpp" "%ROOT%\src\ut_paths.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_store.exe" "%JOUT%" > "%OUT%\test_store.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_store.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_store.out.txt"
exit /b %RC%
