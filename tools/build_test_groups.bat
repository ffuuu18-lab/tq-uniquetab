@echo off
rem build_test_groups.bat - the groups as data, offline: the uniquetab-groups.ini parser and
rem matcher, the auto groups and Other, the G line of uniq-groups.txt (both forms), the player's file
rem merged in front of the built-in set (read only: nothing is ever written, renamed or created
rem beside it), the derived words, and the built-in template over the shipped catalogue: its
rem grouping is data\oracle\uniq-groups.txt's (collect_mi=1) and data\oracle\nomi\'s
rem (collect_mi=0), group for group and record for record. NO GAME NEEDED; writes only under
rem build\test.
rem   tools\build_test_groups.bat        (finds vcvars32 itself, like build.bat)
rem
rem It compiles the REAL headers the generator compiles - src\gen\ut_groups.h and
rem src\gen\ut_groups_default.h - and the model's catalogue loader (src\model\catalogue.cpp).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OUT%\groups" mkdir "%OUT%\groups"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /Fo"%OUT%\groups\\" /Fe"%OUT%\test_groups.exe" ^
   "%ROOT%\tools\test_groups.cpp" "%ROOT%\src\model\catalogue.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_groups.exe" "%ROOT%\data\oracle" "%OUT%\groups" > "%OUT%\test_groups.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_groups.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_groups.out.txt"
exit /b %RC%
