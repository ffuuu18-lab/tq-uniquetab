@echo off
rem build_test_catalogue.bat - the generator (src\gen) offline, x86 as the mod. Reads the Titan
rem Quest AE folder READ ONLY (%TQ_GAME_DIR%, else the Steam registry - the same rule as
rem tools\build_test_bindings.bat), writes only under build\test. NO GAME is launched.
rem
rem pass 1: the decoder and reader refusals (--units): zlib streams, a synthetic ARZ record with
rem         csize 0 / a truncated / a corrupt body, a synthetic ARC part that does not inflate,
rem         GD's ARC version, the UTF-16 tag parser, the model's v1 / footprint refusals.
rem pass 2: catalogue.bin generated from the game folder, byte for byte against data\oracle.
rem pass 3: the DLL's start-up path (--ensure): all four outputs byte for byte against
rem         data\oracle, the stamp, temporary names, the rename, the error paths.
rem pass 4: one generation in a fresh process (--peak): the time and the 200 MB budget.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /O2 /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_catalogue.exe" ^
   "%ROOT%\tools\test_catalogue.cpp" "%ROOT%\src\gen\inflate.cpp" "%ROOT%\src\gen\arz_reader.cpp" ^
   "%ROOT%\src\gen\arc_reader.cpp" "%ROOT%\src\gen\catalogue_gen.cpp" "%ROOT%\src\gen\generate.cpp" ^
   "%ROOT%\src\model\catalogue.cpp" psapi.lib advapi32.lib
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

set RC=0

rem ---- pass 1: the refusals, no game needed ---------------------------------------------------
if not exist "%OUT%\gen-units" mkdir "%OUT%\gen-units"
"%OUT%\test_catalogue.exe" --units "%OUT%\gen-units" "%ROOT%\data\oracle\catalogue.bin"
if errorlevel 1 set RC=1

rem ---- pass 2: catalogue.bin vs the oracle file ------------------------------------------------
"%OUT%\test_catalogue.exe" --catalogue "%ROOT%\data\oracle\catalogue.bin" "%OUT%\gen-catalogue.bin"
if errorlevel 1 set RC=1

rem ---- pass 3: the DLL's start-up path against a scratch mod folder ---------------------------
if not exist "%OUT%\gen-mod" mkdir "%OUT%\gen-mod"
"%OUT%\test_catalogue.exe" --ensure "%ROOT%\data" "%OUT%\gen-mod"
if errorlevel 1 set RC=1

rem ---- pass 4: the peak working set, in a process of its own ---------------------------------
if not exist "%OUT%\gen-peak" mkdir "%OUT%\gen-peak"
"%OUT%\test_catalogue.exe" --peak "%OUT%\gen-peak"
if errorlevel 1 set RC=1

if not "%RC%"=="0" (echo [test] FAILURES) else (echo [test] ALL PASS)
exit /b %RC%
