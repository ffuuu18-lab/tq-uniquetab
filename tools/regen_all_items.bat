@echo off
rem regen_all_items.bat - rebuild the "every item collected" file, data\allitems\tq-uniq-items-all.jsonl
rem (and its report), from the catalogue fixtures.
rem
rem   tools\regen_all_items.bat                   every row generated
rem   tools\regen_all_items.bat "<journal copy>"  that collection's rows carried through byte for
rem                                               byte, only the missing records generated
rem
rem The fixtures come from data\oracle\. When uniq-records.txt is not there (a fresh clone), they
rem are generated first with tools\build_catalogue.py, which finds the game folder itself
rem (%TQ_GAME_DIR%, else the Steam registry - tools\tqpath.py) and only READS it. A journal given
rem here is only read. The output is deterministic apart from the stamps.
setlocal
set "ROOT=%~dp0.."
set "ORACLE=%ROOT%\data\oracle"
set "OUTDIR=%ROOT%\data\allitems"

if exist "%ORACLE%\uniq-records.txt" if exist "%ORACLE%\catalogue.json" goto :have_fixtures
echo [allitems] the catalogue fixtures are missing - generating them from the game (read only)
python "%ROOT%\tools\build_catalogue.py" || (echo [allitems] build_catalogue.py FAILED & exit /b 1)
:have_fixtures

rem No ( ) block around the journal: a ")" in its path - "Program Files (x86)" - would close it.
set "JOURNAL="
if "%~1"=="" goto :no_journal
if not exist "%~1" (echo [allitems] no journal at "%~1" & exit /b 1)
set JOURNAL=--journal "%~1"
:no_journal
if not exist "%OUTDIR%" mkdir "%OUTDIR%"

python "%ROOT%\tools\make_all_items.py" ^
    --records "%ORACLE%\uniq-records.txt" ^
    --catalogue "%ORACLE%\catalogue.json" ^
    --out "%OUTDIR%\tq-uniq-items-all.jsonl" ^
    --report "%OUTDIR%\tq-uniq-items-all.report.txt" %JOURNAL%
if errorlevel 1 (echo [allitems] FAILED & exit /b 1)
echo [allitems] wrote "%OUTDIR%\tq-uniq-items-all.jsonl"
echo [allitems] prove it: tools\build_test_journal.bat loads it (test_journal --load)
exit /b 0
