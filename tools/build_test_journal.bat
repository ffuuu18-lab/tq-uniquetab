@echo off
rem build_test_journal.bat - the journal TQ-1 (src\ut_rescue.cpp), offline. NO GAME NEEDED.
rem TQ port of GD's tools\build_test_journal.bat: x86, and the whole run stays inside
rem build\test\journal-out (the test sets %UNIQUETAB_OUT% to it before any path call).
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
set "JOUT=%OUT%\journal-out"
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
   /Fo"%OUT%\\" /Fe"%OUT%\test_journal.exe" ^
   "%ROOT%\tools\test_journal.cpp" "%ROOT%\src\ut_rescue.cpp" "%ROOT%\src\ut_log.cpp" "%ROOT%\src\ut_paths.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_journal.exe" "%JOUT%" > "%OUT%\test_journal.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_journal.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_journal.out.txt"
if not "%RC%"=="0" exit /b %RC%
rem The all-items file (tools\make_all_items.py): loaded through the reader, every record's take
rem round trip. It is committed in data\allitems\; a tree without it skips this step.
set "ALL=%ROOT%\data\allitems\tq-uniq-items-all.jsonl"
if not exist "%ALL%" (echo [test] no data\allitems\tq-uniq-items-all.jsonl - the load step is skipped & exit /b 0)
"%OUT%\test_journal.exe" "%JOUT%" --load "%ALL%" > "%OUT%\test_journal_load.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_journal_load.out.txt"
exit /b %RC%
