@echo off
setlocal enabledelayedexpansion

REM =============================================================================
REM Diffy Windows Build Script
REM Builds the diffy CLI with CMake + Ninja.
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

REM The CLI always builds with the tests available.
set WANT_TESTS=ON

REM =============================================================================
REM Toolchain detection.
REM MSVC is the default; MinGW (MSYS2 UCRT64) is a fallback.
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
echo Diffy Build ^(CLI^)
echo Configuration: %CMAKE_BUILD_TYPE%
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

REM Configure CMake arguments
set CMAKE_ARGS=-G %GENERATOR% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE%
set CMAKE_ARGS=!CMAKE_ARGS! -DDIFFY_BUILD_CLI=ON -DDIFFY_BUILD_TESTS=%WANT_TESTS%

REM Decide whether a reconfigure is needed (build type changed).
set NEED_RECONFIGURE=0
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_BUILD_TYPE:STRING=%CMAKE_BUILD_TYPE%" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
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
    echo Reconfiguring CMake ^(build type changed^)...
    cmake -S . -B "%BUILD_DIR%" !CMAKE_ARGS!
    if !ERRORLEVEL! neq 0 ( echo Error: CMake reconfiguration failed & exit /b 1 )
    echo.
)

REM Build step. For a plain build, narrow to the diffy target; for `test`, build
REM everything configured so diffy-test is built before ctest runs.
if %RUN_TESTS%==1 (
    set BUILD_TARGETS=
) else (
    set BUILD_TARGETS=--target diffy
)

echo Building %CMAKE_BUILD_TYPE% ^(CLI^)...
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

REM Assemble a self-contained dist folder if requested: the diffy.exe and the
REM tree-sitter grammars. Lands beside the build tree (out\<config>\dist-<tc>).
if %DO_DIST%==1 (
    set "DIST_DIR=%BUILD_DIR:build=dist%"
    echo.
    echo Assembling dist folder: !DIST_DIR!
    if exist "!DIST_DIR!" rd /s /q "!DIST_DIR!"
    mkdir "!DIST_DIR!"
    if exist "%BUILD_DIR%\cli\diffy.exe" copy /y "%BUILD_DIR%\cli\diffy.exe" "!DIST_DIR!" >nul
    if exist "%BUILD_DIR%\grammars" (
        xcopy /y /i /q "%BUILD_DIR%\grammars" "!DIST_DIR!\grammars" >nul
    )
    echo Dist folder ready: !DIST_DIR!
)

REM Run the built CLI if requested
if %DO_RUN%==1 (
    echo.
    set "RUN_EXE=%BUILD_DIR%\cli\diffy.exe"
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
echo Diffy Windows Build Script ^(CLI^)
echo.
echo Usage: build-windows.cmd ^<config^> [options]
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
echo Compiler ^(optional^):
echo   ^(default^)          MSVC when available, otherwise MinGW ^(MSYS2 UCRT64^)
echo   msvc               Force MSVC ^(fail if not found^)
echo   mingw              Force MinGW/MSYS2 UCRT64
echo.
echo Other options:
echo   run                Run the built diffy.exe after building
echo   dist               Copy diffy.exe and the tree-sitter grammars into a
echo                      self-contained out\^<config^>\dist-^<toolchain^> folder
echo   --skip-deps        Skip git submodule initialization
echo.
echo Prerequisites:
echo   - CMake ^(>= 3.21^) and Ninja in PATH
echo   - Visual Studio 2022 C++ workload, or MSYS2 UCRT64 GCC
echo.
echo Examples:
echo   build-windows.cmd release             Build the CLI ^(Release^)
echo   build-windows.cmd debug run           Build and run the CLI ^(Debug^)
echo   build-windows.cmd release dist        Build and assemble dist\
echo   build-windows.cmd test                Build Debug and run tests
echo   build-windows.cmd clean               Remove build outputs
echo.
exit /b 0
