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
if not "%RC%"=="0" exit /b %RC%
rem The developer seeding tool (tools\seed_relic_rows.py) on a copy of the all-items file: a dry run
rem writes nothing; the run keeps a byte-equal backup and appends partial pieces (var1 1..N-1, two
rem sets) that the reader keeps whole and the red slot counts: N 3 -> 2 x (1+2) = 6, N 5 -> 20.
rem The tool is development-only: a copy of the sources without it skips this step.
if not exist "%ROOT%\tools\seed_relic_rows.py" (echo [test] the seeding tool is not in this tree, its run is skipped & exit /b 0)
set "SEEDJ=%JOUT%\seed-relics.jsonl"
set "IRON=records/xpack/item/relics/01_act4_ironwillofajax.dbr"
set "PENG=records/item/animalrelics/01_act3_pengclaw.dbr"
copy /y "%ALL%" "%SEEDJ%" >nul
python "%ROOT%\tools\seed_relic_rows.py" --journal "%SEEDJ%" --relics "%ROOT%\data\oracle\uniq-relics.txt" --record "%IRON%" --record "%PENG%" --sets 2 --skip-game-check --dry-run > "%OUT%\test_seed.out.txt" 2>&1
if errorlevel 1 (type "%OUT%\test_seed.out.txt" & echo [test] the seeding tool's dry run FAILED & exit /b 1)
fc /b "%ALL%" "%SEEDJ%" >nul
if errorlevel 1 (echo [test] the seeding tool's dry run changed the file & exit /b 1)
python "%ROOT%\tools\seed_relic_rows.py" --journal "%SEEDJ%" --relics "%ROOT%\data\oracle\uniq-relics.txt" --record "%IRON%" --record "%PENG%" --sets 2 --skip-game-check >> "%OUT%\test_seed.out.txt" 2>&1
if errorlevel 1 (type "%OUT%\test_seed.out.txt" & echo [test] the seeding tool FAILED & exit /b 1)
fc /b "%ALL%" "%SEEDJ%.seed-backup" >nul
if errorlevel 1 (echo [test] the seeding tool's backup is not the file before & exit /b 1)
"%OUT%\test_journal.exe" "%JOUT%" --shards "%SEEDJ%" "%IRON%" 6 "%PENG%" 20 > "%OUT%\test_journal_seed.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_seed.out.txt"
type "%OUT%\test_journal_seed.out.txt"
exit /b %RC%
