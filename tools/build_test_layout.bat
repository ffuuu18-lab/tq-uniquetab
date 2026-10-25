@echo off
rem build_test_layout.bat - the layout model (src\ut_layout.h) and the pad's pages (src\ut_padpage.h), offline.
rem NO GAME NEEDED. The [layout] text, the pad in a rectangle, the resolution, the scale sweep.
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
   /Fo"%OUT%\\" /Fe"%OUT%\test_layout.exe" "%ROOT%\tools\test_layout.cpp" "%ROOT%\src\model\layout.cpp" ^
   "%ROOT%\src\model\catalogue.cpp" "%ROOT%\src\model\collection.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_layout.exe" > "%OUT%\test_layout.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_layout.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_layout.out.txt"
exit /b %RC%
