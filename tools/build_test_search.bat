@echo off
rem build_test_search.bat - the property search's pure half (src\ut_search.h, src\ut_searchfold.h),
rem offline. NO GAME NEEDED.
rem   tools\build_test_search.bat        (finds vcvars32 itself, like build.bat: x86, as the mod)
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
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_search.exe" "%ROOT%\tools\test_search.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_search.exe" > "%OUT%\test_search.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_search.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_search.out.txt"
exit /b %RC%
