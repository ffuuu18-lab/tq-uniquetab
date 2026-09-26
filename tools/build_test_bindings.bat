@echo off
rem build_test_bindings.bat - THE BINDINGS, OFFLINE. NO GAME IS LAUNCHED.
rem Run it from anywhere:  tools\build_test_bindings.bat   (it finds vcvars32 itself: x86, as the mod)
rem
rem One READ-ONLY input: the installed game folder (the one that holds TQ.exe), from
rem   TQ_GAME_DIR                 if set, else
rem   the Steam registry          SteamPath + libraryfolders.vdf -> steamapps\common\Titan Quest
rem                               Anniversary Edition
rem TQ.exe, Engine.dll, Game.dll are opened READ ONLY and nothing is copied out of the folder; the
rem MSVCR110.dll check uses the system copy (the game loads that one). Without a game folder the
rem file halves SKIP LOUDLY and the gate half still runs.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

rem ut_bindings.cpp is linked in, not copied: the two signatures and the decoders the test runs
rem are THE ONES THE MOD USES. advapi32 is for the read-only Steam registry lookup.
cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /Fo"%OUT%\\" /Fe"%OUT%\test_bindings.exe" ^
   "%ROOT%\tools\test_bindings.cpp" "%ROOT%\src\ut_bindings.cpp" "%ROOT%\src\ut_log.cpp" ^
   "%ROOT%\src\ut_paths.cpp" advapi32.lib
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_bindings.exe" "%ROOT%\src"
set RC=%ERRORLEVEL%
if not "%RC%"=="0" (echo [test] FAILURES) else (echo [test] ALL PASS)
exit /b %RC%
