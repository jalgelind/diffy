@echo off
setlocal enabledelayedexpansion

REM =============================================================================
REM Diffy Windows Build Script
REM Builds the diffy CLI and/or the diffy-gui git client with CMake + Ninja.
REM Prefers MSVC (Visual Studio 2022), falls back to MinGW (MSYS2 UCRT64).
REM Submodules are initialized automatically.
REM =============================================================================

set BUILD_OUT=out
set GENERATOR=Ninja
set NINJA_JOBS=%NUMBER_OF_PROCESSORS%
if "%NINJA_JOBS%"=="" set NINJA_JOBS=8

set USE_MSVC=0
set FORCE_MSVC=0
set FORCE_MINGW=0

REM Parse command line arguments
set BUILD_TYPE=
set RUN_TESTS=0
set CLEAN=0
set RECONFIGURE=0
set DO_RUN=0
set DO_DIST=0
set SKIP_DEPS=0

REM Target selection: cli (diffy), gui (diffy-gui), all. Default = cli + tests.
set TARGET=cli

if "%1"=="" goto :show_help
if "%1"=="help" goto :show_help
if "%1"=="--help" goto :show_help
if "%1"=="-h" goto :show_help

:parse_args
if "%1"=="" goto :args_done
if /I "%1"=="debug" (
    set BUILD_TYPE=Debug
    shift
    goto :parse_args
)
if /I "%1"=="release" (
    set BUILD_TYPE=Release
    shift
    goto :parse_args
)
if /I "%1"=="relwithdebinfo" (
    set BUILD_TYPE=RelWithDebInfo
    shift
    goto :parse_args
)
if /I "%1"=="test" (
    set BUILD_TYPE=Debug
    set RUN_TESTS=1
    shift
    goto :parse_args
)
if /I "%1"=="clean" (
    set CLEAN=1
    shift
    goto :parse_args
)
if /I "%1"=="reconfigure" (
    set RECONFIGURE=1
    shift
    goto :parse_args
)
if /I "%1"=="cli" (
    set TARGET=cli
    shift
    goto :parse_args
)
if /I "%1"=="gui" (
    set TARGET=gui
    shift
    goto :parse_args
)
if /I "%1"=="all" (
    set TARGET=all
    shift
    goto :parse_args
)
if /I "%1"=="run" (
    set DO_RUN=1
    shift
    goto :parse_args
)
if /I "%1"=="dist" (
    set DO_DIST=1
    shift
    goto :parse_args
)
if /I "%1"=="msvc" (
    set FORCE_MSVC=1
    shift
    goto :parse_args
)
if /I "%1"=="mingw" (
    set FORCE_MINGW=1
    shift
    goto :parse_args
)
if /I "%1"=="--skip-deps" (
    set SKIP_DEPS=1
    shift
    goto :parse_args
)
echo Unknown argument: %1
goto :show_help

:args_done

REM Handle clean request
if %CLEAN%==1 (
    echo Cleaning build directory: %BUILD_OUT%
    if exist "%BUILD_OUT%" (
        rd /s /q "%BUILD_OUT%"
        echo Build directory cleaned.
    ) else (
        echo Build directory does not exist.
    )
    exit /b 0
)

REM Validate build type
if "%BUILD_TYPE%"=="" (
    echo Error: No build configuration specified.
    echo.
    goto :show_help
)

REM Map build type to output directory
if /I "%BUILD_TYPE%"=="Debug" (
    set BUILD_DIR=%BUILD_OUT%\debug\build
    set CMAKE_BUILD_TYPE=Debug
) else if /I "%BUILD_TYPE%"=="Release" (
    set BUILD_DIR=%BUILD_OUT%\release\build
    set CMAKE_BUILD_TYPE=Release
) else if /I "%BUILD_TYPE%"=="RelWithDebInfo" (
    set BUILD_DIR=%BUILD_OUT%\relwithdebinfo\build
    set CMAKE_BUILD_TYPE=RelWithDebInfo
)

REM Translate the target into CMake option values.
if /I "%TARGET%"=="cli" (
    set WANT_CLI=ON
    set WANT_GUI=OFF
    set WANT_TESTS=ON
) else if /I "%TARGET%"=="gui" (
    set WANT_CLI=OFF
    set WANT_GUI=ON
    set WANT_TESTS=OFF
) else (
    set WANT_CLI=ON
    set WANT_GUI=ON
    set WANT_TESTS=ON
)
REM `test` always needs the test target.
if %RUN_TESTS%==1 set WANT_TESTS=ON

REM =============================================================================
REM Toolchain detection.
REM MSVC is the default (best supported for Slint and libgit2/vcpkg). MinGW
REM (MSYS2 UCRT64) is a fallback, mainly useful for the CLI.
REM =============================================================================

if %FORCE_MINGW%==1 (
    if exist "C:\msys64\ucrt64\bin\g++.exe" (
        set "PATH=C:\msys64\ucrt64\bin;!PATH!"
        set "COMPILER_NAME=MinGW-w64 GCC (MSYS2 UCRT64)"
        goto :toolchain_done
    )
    echo Error: mingw requested but C:\msys64\ucrt64\bin\g++.exe not found.
    echo Please install MSYS2 with mingw-w64-ucrt-x86_64-gcc.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "!VSWHERE!" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_PATH=%%i"
    )
    if defined VS_INSTALL_PATH (
        set "VCVARSALL=!VS_INSTALL_PATH!\VC\Auxiliary\Build\vcvarsall.bat"
        if exist "!VCVARSALL!" (
            echo Initializing Visual Studio environment...
            call "!VCVARSALL!" x64 >nul
            set USE_MSVC=1
            set "COMPILER_NAME=MSVC (Visual Studio 2022)"
            goto :toolchain_done
        )
    )
)

if %FORCE_MSVC%==1 (
    echo Error: msvc requested but Visual Studio with C++ tools was not found.
    echo Please install Visual Studio 2022 with the "Desktop development with C++" workload.
    exit /b 1
)
if exist "C:\msys64\ucrt64\bin\g++.exe" (
    set "PATH=C:\msys64\ucrt64\bin;!PATH!"
    set "COMPILER_NAME=MinGW-w64 GCC (MSYS2 UCRT64)"
    echo Note: MSVC not found; falling back to MinGW.
    goto :toolchain_done
)

echo Error: No C++ compiler found.
echo Please install Visual Studio 2022 with the C++ workload ^(preferred^),
echo or MSYS2 with mingw-w64-ucrt-x86_64-gcc.
exit /b 1

:toolchain_done

REM Keep MSVC and MinGW caches apart.
if !USE_MSVC!==1 (
    set BUILD_DIR=%BUILD_DIR%-msvc
) else (
    set BUILD_DIR=%BUILD_DIR%-mingw
)

echo =============================================================================
echo Diffy Build
echo Configuration: %CMAKE_BUILD_TYPE%
echo Target:        %TARGET%  ^(CLI=%WANT_CLI% GUI=%WANT_GUI% TESTS=%WANT_TESTS%^)
echo Build Dir:     %BUILD_DIR%
echo Compiler:      %COMPILER_NAME%
echo =============================================================================
echo.

REM Required tools
where cmake >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo Error: CMake not found in PATH. Install CMake ^(>= 3.21^) and add it to PATH.
    exit /b 1
)
where ninja >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo Error: Ninja not found in PATH. Install Ninja and add it to PATH.
    exit /b 1
)

REM =============================================================================
REM Dependencies
REM =============================================================================
if %SKIP_DEPS%==0 (
    if not exist "%BUILD_OUT%\.submodules-initialized" (
        echo Initializing git submodules...
        git submodule update --init --recursive
        if !ERRORLEVEL! neq 0 (
            echo Error: Failed to initialize submodules
            exit /b 1
        )
        if not exist "%BUILD_OUT%" mkdir "%BUILD_OUT%"
        type nul > "%BUILD_OUT%\.submodules-initialized"
        echo Submodules initialized.
        echo.
    )
)

REM The GUI builds Slint from source (needs cargo) and links libgit2 (vcpkg).
if /I not "%WANT_GUI%"=="OFF" (
    where cargo >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        echo Error: cargo ^(Rust toolchain^) not found, but the GUI builds Slint from source.
        echo Install Rust from https://rustup.rs and re-run.
        exit /b 1
    )
)

REM Configure CMake arguments
set CMAKE_ARGS=-G %GENERATOR% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE%
set CMAKE_ARGS=!CMAKE_ARGS! -DDIFFY_BUILD_CLI=%WANT_CLI% -DDIFFY_BUILD_GUI=%WANT_GUI% -DDIFFY_BUILD_TESTS=%WANT_TESTS%

REM The GUI's libgit2 is most easily provided by vcpkg ^(`vcpkg install libgit2`^).
REM When VCPKG_ROOT is set we hand CMake the vcpkg toolchain, which also makes
REM pkg-config/libgit2 discoverable and auto-deploys the runtime DLLs.
if /I not "%WANT_GUI%"=="OFF" (
    if defined VCPKG_ROOT (
        if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
            set CMAKE_ARGS=!CMAKE_ARGS! -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
            echo Using vcpkg toolchain: %VCPKG_ROOT%
        )
    ) else (
        echo Note: VCPKG_ROOT is not set. The GUI needs libgit2; install vcpkg and run
        echo       `vcpkg install libgit2 pkgconf`, then set VCPKG_ROOT, or provide libgit2
        echo       another way ^(CMake must be able to satisfy the gui target's libgit2^).
    )
)

REM Decide whether a reconfigure is needed (build type or target flags changed).
set NEED_RECONFIGURE=0
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_BUILD_TYPE:STRING=%CMAKE_BUILD_TYPE%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !ERRORLEVEL! neq 0 set NEED_RECONFIGURE=1
    findstr /C:"DIFFY_BUILD_CLI:BOOL=%WANT_CLI%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !ERRORLEVEL! neq 0 set NEED_RECONFIGURE=1
    findstr /C:"DIFFY_BUILD_GUI:BOOL=%WANT_GUI%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !ERRORLEVEL! neq 0 set NEED_RECONFIGURE=1
    findstr /C:"DIFFY_BUILD_TESTS:BOOL=%WANT_TESTS%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !ERRORLEVEL! neq 0 set NEED_RECONFIGURE=1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    echo Configuring CMake...
    cmake -S . -B "%BUILD_DIR%" !CMAKE_ARGS!
    if !ERRORLEVEL! neq 0 (
        echo Error: CMake configuration failed
        if exist "%BUILD_DIR%\CMakeCache.txt" del /q "%BUILD_DIR%\CMakeCache.txt"
        exit /b 1
    )
    echo.
) else if %RECONFIGURE%==1 (
    echo Reconfiguring CMake...
    cmake -S . -B "%BUILD_DIR%" !CMAKE_ARGS!
    if !ERRORLEVEL! neq 0 ( echo Error: CMake reconfiguration failed & exit /b 1 )
    echo.
) else if !NEED_RECONFIGURE!==1 (
    echo Reconfiguring CMake ^(build type or target options changed^)...
    cmake -S . -B "%BUILD_DIR%" !CMAKE_ARGS!
    if !ERRORLEVEL! neq 0 ( echo Error: CMake reconfiguration failed & exit /b 1 )
    echo.
)

REM Build step. Narrow to a single target for plain cli/gui builds; build
REM everything configured for `all` or when tests are requested (so diffy-test
REM is built before ctest runs).
if %RUN_TESTS%==1 (
    set BUILD_TARGETS=
) else if /I "%TARGET%"=="cli" (
    set BUILD_TARGETS=--target diffy
) else if /I "%TARGET%"=="gui" (
    set BUILD_TARGETS=--target diffy-gui
) else (
    set BUILD_TARGETS=
)

echo Building %CMAKE_BUILD_TYPE% ^(%TARGET%^)...
cmake --build "%BUILD_DIR%" --parallel %NINJA_JOBS% --config %CMAKE_BUILD_TYPE% !BUILD_TARGETS!
if !ERRORLEVEL! neq 0 (
    echo.
    echo Error: Build failed
    exit /b 1
)

echo.
echo =============================================================================
echo Build completed successfully: %CMAKE_BUILD_TYPE%
echo Output: %BUILD_DIR%
echo =============================================================================

REM Run tests if requested
if %RUN_TESTS%==1 (
    echo.
    echo Running tests...
    ctest --test-dir "%BUILD_DIR%" --output-on-failure
    if !ERRORLEVEL! neq 0 ( echo. & echo Error: Tests failed & exit /b 1 )
    echo.
    echo All tests passed
)

REM Assemble a self-contained dist folder if requested: the built executables,
REM their runtime DLLs (Slint + vcpkg-deployed libgit2 etc.), and the
REM tree-sitter grammars. Lands beside the build tree (out\<config>\dist-<tc>).
if %DO_DIST%==1 (
    set "DIST_DIR=%BUILD_DIR:build=dist%"
    echo.
    echo Assembling dist folder: !DIST_DIR!
    if exist "!DIST_DIR!" rd /s /q "!DIST_DIR!"
    mkdir "!DIST_DIR!"
    if exist "%BUILD_DIR%\cli\diffy.exe" copy /y "%BUILD_DIR%\cli\diffy.exe" "!DIST_DIR!" >nul
    if exist "%BUILD_DIR%\gui\diffy-gui.exe" (
        copy /y "%BUILD_DIR%\gui\diffy-gui.exe" "!DIST_DIR!" >nul
        copy /y "%BUILD_DIR%\gui\*.dll" "!DIST_DIR!" >nul
    )
    if exist "%BUILD_DIR%\grammars" (
        xcopy /y /i /q "%BUILD_DIR%\grammars" "!DIST_DIR!\grammars" >nul
    )
    echo Dist folder ready: !DIST_DIR!
)

REM Run the built app if requested
if %DO_RUN%==1 (
    echo.
    if /I "%TARGET%"=="gui" (
        set "RUN_EXE=%BUILD_DIR%\gui\diffy-gui.exe"
    ) else (
        set "RUN_EXE=%BUILD_DIR%\cli\diffy.exe"
    )
    if exist "!RUN_EXE!" (
        echo Running !RUN_EXE!...
        "!RUN_EXE!"
    ) else (
        echo Error: Executable not found: !RUN_EXE!
        exit /b 1
    )
)

exit /b 0

:show_help
echo Diffy Windows Build Script
echo.
echo Usage: build-windows.cmd ^<config^> [target] [options]
echo.
echo Configuration ^(required^):
echo   debug              Build Debug
echo   release            Build Release
echo   relwithdebinfo     Build RelWithDebInfo
echo   test               Build Debug and run the test suite
echo   clean              Remove all build outputs ^(out\^)
echo   reconfigure        Force a CMake reconfigure
echo   help               Show this help
echo.
echo Target ^(optional, default: cli^):
echo   cli                Build the diffy command-line tool ^(+ tests^)
echo   gui                Build the diffy-gui git client
echo   all                Build the CLI, the GUI, and the tests
echo.
echo Compiler ^(optional^):
echo   ^(default^)          MSVC when available, otherwise MinGW ^(MSYS2 UCRT64^)
echo   msvc               Force MSVC ^(fail if not found^)
echo   mingw              Force MinGW/MSYS2 UCRT64
echo.
echo Other options:
echo   run                Run the built executable after building
echo   dist               Copy executables, runtime DLLs and grammars into a
echo                      self-contained out\^<config^>\dist-^<toolchain^> folder
echo   --skip-deps        Skip git submodule initialization
echo.
echo Prerequisites:
echo   - CMake ^(>= 3.21^) and Ninja in PATH
echo   - CLI: Visual Studio 2022 C++ workload, or MSYS2 UCRT64 GCC
echo   - GUI: additionally a Rust toolchain ^(cargo, for Slint^) and libgit2.
echo          The easiest libgit2 path is vcpkg: install vcpkg, run
echo          `vcpkg install libgit2 pkgconf`, and set VCPKG_ROOT.
echo.
echo Examples:
echo   build-windows.cmd release             Build the CLI ^(Release^)
echo   build-windows.cmd debug cli run       Build and run the CLI ^(Debug^)
echo   build-windows.cmd release gui run     Build and run the GUI ^(Release^)
echo   build-windows.cmd release all         Build CLI + GUI + tests
echo   build-windows.cmd release all dist    Build everything and assemble dist\
echo   build-windows.cmd test                Build Debug and run tests
echo   build-windows.cmd clean               Remove build outputs
echo.
exit /b 0
