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
rem         With collect_mi=1 (the default) every pass includes the monster infrequents; pass 2
rem         and pass 3 also check collect_mi=0 against data\oracle\nomi (the uniques alone).
rem pass 5: collect_unobtainable=1 (--ensure-flag) byte for byte against
rem         tools\build_catalogue.py --collect-unobtainable --collect-mi (the three lists, and catalogue.bin
rem         through tools\pack_catalogue.py), and tools\reach_scan.py --assert-rule:
rem         the records EXCLUDE rule 7 drops are exactly the ones no level, quest, monster, chest
rem         or merchant can give (Python; a game update that adds a holder is caught here).
rem pass 6: first, always, the search order: test_catalogue --order builds a scratch game whose
rem         Items.arc holds one entry of the base game's, and the C++ and tools\tqarc.py must both
rem         answer the game's archive for it and the base archive for an entry only the base holds
rem         (the game's archives first, entry by entry; writes only under build\test\game-order).
rem         Then the custom games under %TQ_TEST_MODS_DIR% (tools\test_gameview.py; skipped with a line
rem         when it is not set or holds none): per game the C++ set byte for byte against
rem         tools\build_catalogue.py --game-mod, the gray icons counted and sampled, and the
rem         measurements (records, groups, time, peak, index and owned-table bytes). Writes only
rem         under build\test\game.
rem pass 6b: always, a custom game's unreadable archives: test_catalogue --zeroarc builds a scratch
rem         game whose Text.arc and Items.arc are zeroed; both are skipped with one warning each
rem         and the catalogue is the base game's; tools\build_catalogue.py --game-mod on the same
rem         game must skip the same two (one WARN each) and write the same files byte for byte.
rem         Writes only under build\test\game-zeroarc.
rem pass 7: every custom game built as at start-up (TQ_TEST_MODS_DIR), the address space.
rem pass 8: the in-game reader (test_catalogue --reader): the base game's set from pass 3 and every
rem         start-up set of pass 7, each read as the DLL reads it at a game's entry - the P line of
rem         its uniq-geometry.txt gives the game's area, and uniq-groups.txt is read and packed on
rem         that area (gdut::readGameGroups, the code ut_live.cpp liveInit runs).
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
   "%ROOT%\src\gen\relics_gen.cpp" ^
   "%ROOT%\src\gen\ui_geometry.cpp" "%ROOT%\src\gen\gen_heap.cpp" ^
   "%ROOT%\src\model\catalogue.cpp" "%ROOT%\src\model\collection.cpp" "%ROOT%\src\model\layout.cpp" ^
   psapi.lib advapi32.lib
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

rem ---- pass 5: collect_unobtainable=1 against the Python reference, and the reachability scan --
if not exist "%OUT%\gen-flag" mkdir "%OUT%\gen-flag"
"%OUT%\test_catalogue.exe" --ensure-flag "%OUT%\gen-flag"
if errorlevel 1 set RC=1
python "%ROOT%\tools\build_catalogue.py" --collect-unobtainable --collect-mi --out "%OUT%\py-flag" >nul
if errorlevel 1 (echo [test] FAIL: build_catalogue.py --collect-unobtainable --collect-mi & set RC=1)
for %%F in (uniq-records.txt uniq-groups.txt uniq-excluded.txt uniq-geometry.txt uniq-relics.txt) do (
    %SystemRoot%\System32\fc.exe /b "%OUT%\gen-flag\%%F" "%OUT%\py-flag\%%F" >nul && (echo [test] ok  : collect_unobtainable=1 %%F is byte-identical to the Python reference) || (echo [test] FAIL: collect_unobtainable=1 %%F differs from the Python reference & set RC=1)
)
python "%ROOT%\tools\pack_catalogue.py" --in "%OUT%\py-flag\catalogue.json" --out "%OUT%\py-flag\catalogue.bin" >nul
if errorlevel 1 (echo [test] FAIL: pack_catalogue.py on the collect_unobtainable=1 catalogue.json & set RC=1)
%SystemRoot%\System32\fc.exe /b "%OUT%\gen-flag\catalogue.bin" "%OUT%\py-flag\catalogue.bin" >nul && (echo [test] ok  : collect_unobtainable=1 catalogue.bin is byte-identical to the Python reference) || (echo [test] FAIL: collect_unobtainable=1 catalogue.bin differs from the Python reference & set RC=1)
python "%ROOT%\tools\reach_scan.py" --assert-rule
if errorlevel 1 (echo [test] FAIL: EXCLUDE rule 7 and the reachability scan disagree & set RC=1)

rem ---- pass 6: the custom games (TQ_TEST_MODS_DIR) against the Python reference, and their cost --
python "%ROOT%\tools\test_gameview.py" --exe "%OUT%\test_catalogue.exe" --out "%OUT%\game"
if errorlevel 1 set RC=1

rem ---- pass 6b: a custom game's unreadable archives are skipped, in the C++ and in the Python twin --
if exist "%OUT%\game-zeroarc" rd /s /q "%OUT%\game-zeroarc"
"%OUT%\test_catalogue.exe" --zeroarc "%OUT%\game-zeroarc" "%ROOT%\data\oracle"
if errorlevel 1 set RC=1
python "%ROOT%\tools\build_catalogue.py" --game-mod "%OUT%\game-zeroarc\zeroarcmod" --collect-mi --out "%OUT%\game-zeroarc\py" > "%OUT%\game-zeroarc\py.txt"
if errorlevel 1 (echo [test] FAIL: build_catalogue.py --game-mod on the zeroed archives & set RC=1)
python "%ROOT%\tools\pack_catalogue.py" --in "%OUT%\game-zeroarc\py\catalogue.json" --out "%OUT%\game-zeroarc\py\catalogue.bin" >nul
if errorlevel 1 (echo [test] FAIL: pack_catalogue.py on the zeroed archives' catalogue.json & set RC=1)
for %%F in (catalogue.bin uniq-records.txt uniq-groups.txt uniq-excluded.txt uniq-geometry.txt uniq-relics.txt) do (
    %SystemRoot%\System32\fc.exe /b "%OUT%\game-zeroarc\cpp\%%F" "%OUT%\game-zeroarc\py\%%F" >nul && (echo [test] ok  : zeroed archives: %%F is byte-identical to the Python reference) || (echo [test] FAIL: zeroed archives: %%F differs from the Python reference & set RC=1)
)
set "ZW=0"
for /f %%N in ('%SystemRoot%\System32\find.exe /c "WARN skips an unreadable archive" ^< "%OUT%\game-zeroarc\py.txt"') do set "ZW=%%N"
if "%ZW%"=="2" (echo [test] ok  : the Python reference skips the same two archives, one WARN each) else (echo [test] FAIL: the Python reference printed %ZW% skip WARN lines, not 2 & set RC=1)
rem the twin's text under a translation: Text_EN.arc first, Text_RU.arc over it (no Text_RU.arc: exit 2, a skip)
python -c "import os, sys; sys.path.insert(0, r'%ROOT%\tools'); import tqarc, tqpath; g = tqpath.game_dir(); ru = os.path.join(g, 'Text', 'Text_RU.arc'); os.path.isfile(ru) or sys.exit(2); en = tqarc.load_text_tags(g, 'EN'); pure = tqarc._merge_text_arc(ru, 'text', {}); m = tqarc.load_text_tags(g, 'RU'); bad = [k for k in en if m.get(k) != pure.get(k, en[k])] + [k for k in pure if m.get(k) != pure[k]]; print('[test] %%s: Python: text_language=RU over Text_EN.arc - %%d tag(s) only English has keep their English text' %% ('FAIL' if bad else 'ok  ', len(set(en) - set(pure)))); sys.exit(1 if bad else 0)"
if errorlevel 2 (echo [test] skip: Text_RU.arc is not installed - the Python translation case did not run) else if errorlevel 1 set RC=1

rem ---- pass 6c: the collection rule on fixture records outside the item folders, C++ and the twin --
if exist "%OUT%\game-rulefix" rd /s /q "%OUT%\game-rulefix"
"%OUT%\test_catalogue.exe" --rulefix "%OUT%\game-rulefix"
if errorlevel 1 set RC=1
python "%ROOT%\tools\build_catalogue.py" --game-mod "%OUT%\game-rulefix\rulefixmod" --collect-mi --out "%OUT%\game-rulefix\py" > "%OUT%\game-rulefix\py.txt"
if errorlevel 1 (echo [test] FAIL: build_catalogue.py --game-mod on the fixture records & set RC=1)
python "%ROOT%\tools\pack_catalogue.py" --in "%OUT%\game-rulefix\py\catalogue.json" --out "%OUT%\game-rulefix\py\catalogue.bin" >nul
if errorlevel 1 (echo [test] FAIL: pack_catalogue.py on the fixture records' catalogue.json & set RC=1)
for %%F in (catalogue.bin uniq-records.txt uniq-groups.txt uniq-excluded.txt uniq-geometry.txt uniq-relics.txt) do (
    %SystemRoot%\System32\fc.exe /b "%OUT%\game-rulefix\cpp\%%F" "%OUT%\game-rulefix\py\%%F" >nul && (echo [test] ok  : fixture records: %%F is byte-identical to the Python reference) || (echo [test] FAIL: fixture records: %%F differs from the Python reference & set RC=1)
)

rem ---- pass 7: every custom game built as at start-up (TQ_TEST_MODS_DIR), the address space ----
if not defined TQ_TEST_MODS_DIR goto :no_startup
if not exist "%OUT%\startup" mkdir "%OUT%\startup"
"%OUT%\test_catalogue.exe" --startup "%TQ_TEST_MODS_DIR%" "%OUT%\startup"
if errorlevel 1 set RC=1
rem each start-up set (made in a private heap) byte for byte against pass 6's (a process per game)
for /d %%G in ("%OUT%\startup\mods\*") do for %%F in (catalogue.bin uniq-records.txt uniq-groups.txt uniq-excluded.txt uniq-geometry.txt uniq-relics.txt) do %SystemRoot%\System32\fc.exe /b "%%G\%%F" "%OUT%\game\%%~nxG\cpp\%%F" >nul || (echo [test] FAIL: the start-up set of %%~nxG: %%F differs from pass 6's & set RC=1)
echo [test] pass 7: every start-up set compared byte for byte with pass 6's (a FAIL line above names any difference)
goto :startup_done
:no_startup
echo [test] skip: pass 7 (the custom games built as at start-up) needs TQ_TEST_MODS_DIR
:startup_done

rem ---- pass 8: every generated set through the in-game reader, on the game's own area ----------
if defined TQ_TEST_MODS_DIR (
    "%OUT%\test_catalogue.exe" --reader "%OUT%\gen-mod" "%OUT%\startup\mods"
) else (
    echo [test] pass 8 reads the base game's set only - the custom games' need TQ_TEST_MODS_DIR
    "%OUT%\test_catalogue.exe" --reader "%OUT%\gen-mod"
)
if errorlevel 1 set RC=1

if not "%RC%"=="0" (echo [test] FAILURES) else (echo [test] ALL PASS)
exit /b %RC%
