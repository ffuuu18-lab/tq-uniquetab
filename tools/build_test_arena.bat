@echo off
rem build_test_arena.bat - the build arena (src\gen\gen_heap.cpp), offline: the reserve (top-down,
rem nothing committed), the allocator (reuse, split, merge, a random churn checked byte for byte),
rem the reset (decommitted, the next pair at the base), the outgrow path (std::bad_alloc and
rem genHeapOutgrown), a free from another thread (refused), the release in one piece, and a pair's
rem private heap without an arena. NO GAME NEEDED; writes only under build\test.
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\build\test"
if not exist "%OUT%\arena" mkdir "%OUT%\arena"

if "%VSCMD_ARG_TGT_ARCH%"=="x86" goto :have_env
call "%ROOT%\tools\find_vcvars.bat"
if errorlevel 1 (echo [test] no x86 C++ toolchain - see the message above & exit /b 1)
call "%VCVARS%" >nul
:have_env

cl /nologo /W4 /WX /EHsc /O2 /std:c++17 /GR- /MT /DNDEBUG /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src" /Fo"%OUT%\arena\\" /Fe"%OUT%\test_arena.exe" ^
   "%ROOT%\tools\test_arena.cpp" "%ROOT%\src\gen\gen_heap.cpp"
if errorlevel 1 (echo [test] BUILD FAILED & exit /b 1)

"%OUT%\test_arena.exe" > "%OUT%\test_arena.out.txt" 2>&1
set "RC=%ERRORLEVEL%"
type "%OUT%\test_arena.out.txt"
if not "%RC%"=="0" echo [test] the transcript is in "%OUT%\test_arena.out.txt"
exit /b %RC%
