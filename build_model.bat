@echo off
rem build_model.bat - build and run the model console test.
rem Nothing here touches the game: it compiles src\model\*.cpp plus
rem tests\model_test.cpp into build\model\model_test.exe (x86, as the mod) and runs it
rem against data\oracle\catalogue.bin - the GD oracle fixture, copied: the model and its
rem binary format are game-neutral, and the Titan Quest catalogue is generated on.  Output is kept in
rem tests\model_test.out.txt.  build\ is gitignored, and bin\ belongs to
rem build.bat, so nothing here writes there.
setlocal enableextensions
set "ROOT=%~dp0"

call "%ROOT%tools\find_vcvars.bat"
if errorlevel 1 (
    echo ERROR: no x86 C++ toolchain - see the message above
    exit /b 1
)
if not defined VSCMD_ARG_TGT_ARCH (
    call "%VCVARS%" >nul
    if errorlevel 1 (
        echo ERROR: vcvars32.bat failed
        exit /b 1
    )
)
if not "%VSCMD_ARG_TGT_ARCH%"=="x86" (
    echo ERROR: this shell targets %VSCMD_ARG_TGT_ARCH%, not x86 - run it from a plain prompt
    exit /b 1
)

pushd "%ROOT%"
if not exist "build\model" mkdir "build\model"
if not exist "tests" mkdir "tests"

echo [1/4] repacking data\oracle\catalogue.bin (idempotency check)
rem data\oracle\catalogue.json is the INTERMEDIATE that tools\build_catalogue.py writes and
rem tools\pack_catalogue.py packs into the 480 KB data\oracle\catalogue.bin the harnesses compare
rem against. Only the packed file is tracked - the 4 MB of JSON is regenerated from the game's own database, so
rem shipping it as well would be 4 MB of derived data nobody reads. Where it is absent there is
rem nothing to repack and the binary is used exactly as it is.
rem The json is only ever packed when it is NEWER than the committed bin (--if-newer): a leftover
rem json from an older run - or a bin a checkout or merge has just replaced - must never be packed
rem over the committed file.
if not exist "data\oracle\catalogue.json" goto :compile
python tools\pack_catalogue.py --if-newer
if errorlevel 1 (
    echo ERROR: pack_catalogue.py failed
    popd
    exit /b 1
)
goto :compile

:compile

echo [2/4] compiling (cl /std:c++17 /W4 /WX /EHsc, x86)
cl /nologo /std:c++17 /W4 /WX /EHsc /permissive- /Zc:__cplusplus /O2 /MT ^
   /Fo"build\model\\" /Fd"build\model\model_test.pdb" /Fe"build\model\model_test.exe" ^
   src\model\catalogue.cpp src\model\collection.cpp src\model\layout.cpp tests\model_test.cpp
if errorlevel 1 (
    echo ERROR: compilation failed
    popd
    exit /b 1
)

echo [3/4] running build\model\model_test.exe data\oracle\catalogue.bin
build\model\model_test.exe data\oracle\catalogue.bin > "tests\model_test.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "tests\model_test.out.txt"
if not "%RC%"=="0" goto :done
rem tests\uniq-groups-7fields.txt is the groups file a 1.0 install has: G lines without the id
rem and the tag, which the reader takes from the built-in table by the label. Every check runs
rem over it as well.
echo [4/4] running build\model\model_test.exe over tests\uniq-groups-7fields.txt
build\model\model_test.exe data\oracle\catalogue.bin tests\uniq-groups-7fields.txt > "build\model\model_test_7fields.txt" 2>&1
set "RC=%ERRORLEVEL%"
if "%RC%"=="0" (
    echo       the 7-field groups file: MODEL TESTS PASSED
) else (
    type "build\model\model_test_7fields.txt"
)
:done
popd
if not "%RC%"=="0" (
    echo.
    echo MODEL TESTS FAILED ^(exit code %RC%^)
    exit /b %RC%
)
exit /b 0
