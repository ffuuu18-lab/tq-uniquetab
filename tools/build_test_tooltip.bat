@echo off
rem build_test_tooltip.bat - the tooltip line (the borrowed N+1 swap, the latch rule, the memo, the
rem texts, the classes, the read-back, the fault latch), offline. NO GAME NEEDED.
rem   tools\build_test_tooltip.bat        (finds vcvars32 itself, like build.bat: x86, as the mod)
rem
rem It links the REAL src\ut_tooltip.cpp and src\ut_config.cpp with a stubbed trampoline and stubbed
rem catalogue / journal lookups, so the swap the test exercises is byte for byte the swap the game runs.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
set "MINHOOK=%ROOT%\third_party\minhook"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DUT_TOOLTIP_TEST_SEAM /I"%MINHOOK%\include" /I"%ROOT%\src" ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_tooltip.exe" ^
   "%ROOT%\tools\test_tooltip.cpp" "%ROOT%\src\ut_tooltip.cpp" "%ROOT%\src\ut_config.cpp" ^
   "%ROOT%\src\ut_paths.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_tooltip.exe" > "%OUT%\test_tooltip.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_tooltip.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_tooltip.out.txt"
exit /b %RC%
