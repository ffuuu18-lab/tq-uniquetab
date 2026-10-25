@echo off
rem undeploy.bat - remove the mod from the game folder, restoring a stock installation.
rem
rem The Ultimate ASI Loader next to TQ.exe is NOT this mod and is never removed here: other .asi
rem plugins may depend on it. Delete it by hand once nothing else needs it.
rem
rem   undeploy.bat "<the folder that holds TQ.exe>"   use this installation
rem   undeploy.bat                                     find it through %TQ_GAME_DIR% or the Steam registry
rem   undeploy.bat "<folder>" --purge                  also delete the files the mod GENERATED in
rem                                                    scripts\uniquetab\ - REFUSED while a journal holds rows
rem
rem The registry lookup finds only the DEFAULT Steam library. A game in a secondary library
rem (another drive, see steamapps\libraryfolders.vdf) needs the argument or %TQ_GAME_DIR%.
rem
rem THE SAFETY INTERLOCK (GD's): scripts\uniquetab\ and each mods\<mod>\ folder under it hold the
rem journals tq-uniq-items*.jsonl, the ONLY copy of every deposited unique. --purge runs
rem tools\journal_guard.ps1 first - it reads every tq-uniq-items*.jsonl of the folder and of every
rem mods\<mod>\, whatever the rest of its name, and of the folder journal_dir names in uniquetab.ini
rem (and its mods\<mod>\) when that is set - and deletes nothing when it exits non-zero (a
rem journal with rows, an unreadable journal) or when the script is missing. Even then the purge
rem deletes only the generated files it names below; your own files and mods\ are never touched.
rem Without --purge the folder is never touched. Empty the collection in the game, or move the
rem journals away by hand, before a purge.
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "PURGE="
set "ARG=%~1"
if /i "%ARG%"=="--purge" (set "PURGE=1" & set "ARG=")
if /i "%~2"=="--purge" set "PURGE=1"

rem ---- where the game is (same rule as deploy.bat) ----------------------------------------------
set "GAME=%ARG%"
if not defined GAME set "GAME=%TQ_GAME_DIR%"
if not defined GAME (
    for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| %SystemRoot%\System32\find.exe "SteamPath"`) do set "STEAM=%%B"
    if not defined STEAM (
        for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Software\Valve\Steam" /v InstallPath 2^>nul ^| %SystemRoot%\System32\find.exe "InstallPath"`) do set "STEAM=%%B"
    )
    if defined STEAM (
        set "STEAM=!STEAM:/=\!"
        set "GAME=!STEAM!\steamapps\common\Titan Quest Anniversary Edition"
    )
)
if not defined GAME (
    echo [undeploy] ERROR: the Titan Quest folder was not found.
    echo            Pass it:  undeploy.bat "<the folder that holds TQ.exe>"
    echo            or set TQ_GAME_DIR to it.
    exit /b 1
)
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"
if not exist "%GAME%\TQ.exe" (
    echo [undeploy] ERROR: "%GAME%\TQ.exe" not found - that is not a Titan Quest folder.
    echo            A game in a secondary Steam library is not found through the registry: pass
    echo            the folder that holds TQ.exe, or set TQ_GAME_DIR to it.
    exit /b 1
)
set "SCRIPTS=%GAME%\scripts"
set "MODDIR=%SCRIPTS%\uniquetab"
set "DST=%SCRIPTS%\uniquetab.asi"
echo [undeploy] game "%GAME%"

rem ---- the .asi FIRST ------------------------------------------------------------------------------
rem A loaded .asi cannot be deleted. Removing it before the mod folder means a running game makes
rem the script refuse with nothing changed, instead of leaving a half-purged folder next to a
rem plugin that is still deployed.
if exist "%DST%" (
    del /q "%DST%" >nul 2>&1
    if exist "%DST%" (
        echo [undeploy] ERROR: could not delete "%DST%" - is the game running? Nothing was changed.
        goto :refuse
    )
    echo [undeploy] removed "%DST%"
) else (
    echo [undeploy] "%DST%" does not exist
)

rem ---- the mod folder (only with --purge, and only once the .asi is gone) ----------------------
rem It holds the player's own ini, the log and the journals (the collection). Without --purge it
rem stays. With --purge the journal guard decides first (GD undeploy.bat's interlock).
if not defined PURGE (
    if exist "%MODDIR%" echo [undeploy] "%MODDIR%" kept - it holds your ini, the log and the collection journals.
    goto :done
)
if not exist "%MODDIR%" goto :done
if not exist "%ROOT%tools\journal_guard.ps1" (
    echo [undeploy] ERROR: tools\journal_guard.ps1 is missing - the collection cannot be checked.
    echo [undeploy]        --purge ABORTED: nothing in "%MODDIR%" was deleted.
    goto :refuse
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%tools\journal_guard.ps1" -OutDir "%MODDIR%"
if errorlevel 1 (
    echo [undeploy] --purge ABORTED: a journal in "%MODDIR%", its mods\ folders or journal_dir holds rows or cannot be read.
    echo [undeploy]        Nothing in the folder was deleted. The .asi removal above stands.
    goto :refuse
)

rem GD's purge: only the files the mod GENERATES are deleted, each by its name. KEPT, because they
rem are yours: uniquetab.ini (the settings), uniquetab-buttons.ini (the buttons, written once,
rem then yours), uniquetab-groups.ini (yours: the mod never writes it), an old uniquetab-text.ini,
rem the collection (tq-uniq-items*.jsonl whatever the rest of the name, their .bad- copies), the
rem spreadsheet copies (tq-uniq-items*.csv), uniquetab.log and its older copies, and
rem uniquetab.state (where the collection was left; deleting it only resets that). mods\ holds one
rem folder per mod - its collection, its own settings and buttons, and its generated set - and is
rem never touched. Any file this list does not name stays, so the folder is left holding them;
rem delete it by hand when you are done.
for %%N in (catalogue.bin catalogue.stamp uniq-records.txt uniq-groups.txt uniq-excluded.txt uniq-geometry.txt uniq-relics.txt uniq-search.txt) do (
    if exist "%MODDIR%\%%N" (
        del /q "%MODDIR%\%%N"
        if exist "%MODDIR%\%%N" (echo [undeploy] WARNING: could not delete "%MODDIR%\%%N") else (echo [undeploy] removed "%MODDIR%\%%N")
    )
)
rem The gray icons (ug<16 hex digits>.tex and gray.stamp) and the layout plates (uniq_plate_*.tex
rem and their .src): no other name in those two folders is ever written or removed, and each
rem folder goes only once it is empty.
if exist "%MODDIR%\gray" (
    del /q "%MODDIR%\gray\ug*.tex" 2>nul
    del /q "%MODDIR%\gray\gray.stamp" 2>nul
    rd "%MODDIR%\gray" 2>nul
    if exist "%MODDIR%\gray" (echo [undeploy] WARNING: "%MODDIR%\gray" kept - it holds a file the mod did not write) else (echo [undeploy] removed "%MODDIR%\gray")
)
if exist "%MODDIR%\plates" (
    del /q "%MODDIR%\plates\uniq_plate_*.tex" 2>nul
    del /q "%MODDIR%\plates\uniq_plate_*.src" 2>nul
    rd "%MODDIR%\plates" 2>nul
    if exist "%MODDIR%\plates" (echo [undeploy] WARNING: "%MODDIR%\plates" kept - it holds a file the mod did not write) else (echo [undeploy] removed "%MODDIR%\plates")
)
rd "%MODDIR%" 2>nul
if exist "%MODDIR%" (
    echo [undeploy] "%MODDIR%" kept - it still holds your own files ^(the collection, the ini files, the logs, mods\^).
) else (
    echo [undeploy] removed "%MODDIR%"
)

:done

echo [undeploy] the ASI loader next to TQ.exe was not touched - remove it by hand if nothing
echo [undeploy] else needs it.
exit /b 0

rem A refusal leaves through here and not through an exit /b inside a ( ) block: run as
rem cmd /c undeploy.bat, a failed del earlier in the same block made that exit /b return 0.
:refuse
exit /b 1
