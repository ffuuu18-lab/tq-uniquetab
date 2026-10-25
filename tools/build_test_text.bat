@echo off
rem build_test_text.bat - the tab's words, offline (src\ut_textcfg.h): the UTF-8 helper, the label
rem templates with {found}, the built-in words, the chain; the pad's template expander
rem (src\ut_padlayout.h) against TQ 1.0.1's label; the game's fonts (src\ut_sfnt.h,
rem src\ut_textmeasure.h) over the REAL Resources\Fonts.arc of the Titan Quest AE folder, READ ONLY
rem (%TQ_GAME_DIR%, else tools\tqpath.py). Writes only under build\test.
rem   tools\build_test_text.bat        (finds vcvars32 itself, like build.bat: x86, as the mod)
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

if not defined TQ_GAME_DIR for /f "usebackq delims=" %%d in (`python "%ROOT%\tools\tqpath.py"`) do set "TQ_GAME_DIR=%%d"
cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /Fo"%OUT%\\" /Fe"%OUT%\test_text.exe" ^
   "%ROOT%\tools\test_text.cpp" "%ROOT%\src\gen\inflate.cpp" "%ROOT%\src\gen\arz_reader.cpp" ^
   "%ROOT%\src\gen\arc_reader.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_text.exe" > "%OUT%\test_text.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_text.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_text.out.txt"
exit /b %RC%
