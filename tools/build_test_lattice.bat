@echo off
rem build_test_lattice.bat - the lattice painter (src\gen\lattice_gen.*), offline.
rem   tools\build_test_lattice.bat        (finds vcvars32 itself, like build.bat)
rem
rem Part 1 needs no game: a synthetic background with a known lattice, painted and sampled.
rem Part 2 reads the installed game's caravan background (read only; %TQ_GAME_DIR% or Steam).
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
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_lattice.exe" ^
   "%ROOT%\tools\test_lattice.cpp" "%ROOT%\src\gen\lattice_gen.cpp" "%ROOT%\src\gen\arc_reader.cpp" ^
   "%ROOT%\src\gen\inflate.cpp" "%ROOT%\src\gen\arz_reader.cpp" advapi32.lib
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_lattice.exe" > "%OUT%\test_lattice.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_lattice.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_lattice.out.txt"
exit /b %RC%
