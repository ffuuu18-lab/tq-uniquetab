@echo off
rem deploy.bat - copy the built mod into the game folder. Exactly one thing is added to the
rem installation: scripts\uniquetab.asi. The mod creates scripts\uniquetab\ itself on its first
rem launch, for its ini and its log. undeploy.bat removes both again.
rem
rem The .asi is loaded by Ultimate ASI Loader, which is NOT part of this mod and is never
rem installed, replaced or removed by these scripts. It has to sit next to TQ.exe already (as
rem dinput8.dll, winmm.dll or xinput1_3.dll); this script refuses to deploy without it unless
rem --no-loader-check is passed.
rem
rem   deploy.bat "<the folder that holds TQ.exe>"   use this installation
rem   deploy.bat                                     find it through %TQ_GAME_DIR% or the Steam registry
rem   deploy.bat "<folder>" --no-loader-check        deploy even though no loader is there yet
rem
rem The registry lookup finds only the DEFAULT Steam library. A game in a secondary library
rem (another drive, see steamapps\libraryfolders.vdf) needs the argument or %TQ_GAME_DIR%.
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "SRC=%ROOT%bin\uniquetab.asi"

rem ---- the arguments ---------------------------------------------------------------------------
rem Read as %~1 / %~2 and not with a for loop, so a path with spaces or brackets stays one word.
set "NOLOADER="
set "ARG=%~1"
if /i "%ARG%"=="--no-loader-check" (set "NOLOADER=1" & set "ARG=")
if /i "%~2"=="--no-loader-check" set "NOLOADER=1"

rem ---- where the game is -----------------------------------------------------------------------
rem 1. the argument, 2. %TQ_GAME_DIR%, 3. the Steam client's own path in the registry. No drive
rem letter and no user folder is written down anywhere in this file.
set "GAME=%ARG%"
if not defined GAME set "GAME=%TQ_GAME_DIR%"
if not defined GAME (
    for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Software\Valve\Steam" /v SteamPath 2^>nul ^| find "SteamPath"`) do set "STEAM=%%B"
    if not defined STEAM (
        for /f "usebackq tokens=2,*" %%A in (`reg query "HKCU\Software\Valve\Steam" /v InstallPath 2^>nul ^| find "InstallPath"`) do set "STEAM=%%B"
    )
    if defined STEAM (
        set "STEAM=!STEAM:/=\!"
        set "GAME=!STEAM!\steamapps\common\Titan Quest Anniversary Edition"
    )
)
if not defined GAME (
    echo [deploy] ERROR: the Titan Quest folder was not found.
    echo          Pass it:  deploy.bat "<the folder that holds TQ.exe>"
    echo          or set TQ_GAME_DIR to it, or install Steam so the registry knows where it is.
    exit /b 1
)
if "%GAME:~-1%"=="\" set "GAME=%GAME:~0,-1%"
set "SCRIPTS=%GAME%\scripts"
set "DST=%SCRIPTS%\uniquetab.asi"

if not exist "%GAME%\TQ.exe" (
    echo [deploy] ERROR: "%GAME%\TQ.exe" not found - that is not a Titan Quest folder.
    echo          A game in a secondary Steam library is not found through the registry: pass
    echo          the folder that holds TQ.exe, or set TQ_GAME_DIR to it.
    exit /b 1
)
if not exist "%SRC%" (
    echo [deploy] ERROR: %SRC% not built. Run build.bat first.
    exit /b 1
)
echo [deploy] game "%GAME%"

rem ---- the loader ------------------------------------------------------------------------------
rem Ultimate ASI Loader ships under one of several system DLL names; the version resource is
rem what says which of them is the loader and which is somebody else's file. Nothing here writes
rem to it - the loader is the player's to install and to remove.
set "LOADER="
for /f "usebackq delims=" %%L in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "$d='%GAME%'; foreach($n in 'dinput8.dll','winmm.dll','xinput1_3.dll'){$p=Join-Path $d $n; if(Test-Path -LiteralPath $p){$pn=(Get-Item -LiteralPath $p).VersionInfo.ProductName; if($pn -and $pn -match 'ASI.?Loader'){$n; break}}}" 2^>nul`) do set "LOADER=%%L"
if defined LOADER (
    echo [deploy] loader "%GAME%\%LOADER%" - left exactly as it is
) else (
    if defined NOLOADER (
        echo [deploy] --no-loader-check: no Ultimate ASI Loader next to TQ.exe - deploying anyway.
        echo [deploy]                    Nothing will load the .asi until one is put there.
    ) else (
        echo [deploy] ERROR: no Ultimate ASI Loader next to "%GAME%\TQ.exe".
        echo          The mod is an .asi plugin: something has to load it. Get the Win32 build of
        echo          Ultimate ASI Loader from https://github.com/ThirteenAG/Ultimate-ASI-Loader
        echo          and put its dinput8.dll next to TQ.exe.
        echo          Then run this script again, or pass --no-loader-check to deploy without it.
        exit /b 1
    )
)

if not exist "%SCRIPTS%" mkdir "%SCRIPTS%"
if not exist "%SCRIPTS%" (
    echo [deploy] ERROR: could not create "%SCRIPTS%"
    exit /b 1
)


rem Archive the .asi that is in the game folder RIGHT NOW, before it is replaced, so a crash dump
rem can always be matched byte for byte against the binary that ran. The copy is named after that
rem file's own last-write time, so bin\history reads as a timeline.
if exist "%DST%" (
    if not exist "%ROOT%bin\history" mkdir "%ROOT%bin\history"
    for %%F in ("%DST%") do set "STAMP=%%~tF"
    set "STAMP=!STAMP::=!"
    set "STAMP=!STAMP: =-!"
    set "STAMP=!STAMP:.=!"
    set "STAMP=!STAMP:/=!"
    copy /y "%DST%" "%ROOT%bin\history\uniquetab-!STAMP!.asi" >nul
    if errorlevel 1 (
        echo [deploy] ERROR: could not archive the .asi already in the game folder - refusing to
        echo          overwrite it, because a crash dump could then never be matched to it.
        exit /b 1
    )
    echo [deploy] archived the previous .asi -^> bin\history\uniquetab-!STAMP!.asi
)

copy /y "%SRC%" "%DST%" >nul
if errorlevel 1 (
    echo [deploy] ERROR: copy failed - is the game running?
    exit /b 1
)
echo [deploy] OK -^> "%DST%"
for %%F in ("%DST%") do echo [deploy] %%~zF bytes, %%~tF

echo [deploy] done. The mod writes its ini and its log into "%SCRIPTS%\uniquetab".
exit /b 0

rem A refusal leaves through here and not through an exit /b inside a ( ) block: run as
rem cmd /c deploy.bat, a failed del earlier in the same block made that exit /b return 0.
:refuse
exit /b 1
