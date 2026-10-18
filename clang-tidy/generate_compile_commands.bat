@echo off
REM Generate compile_commands.json convenience script
REM 
REM Usage:
REM   generate_compile_commands.bat                    - Use default settings
REM   generate_compile_commands.bat ..\build_Debug       - Specify build directory
REM   generate_compile_commands.bat ..\build_Debug ..\compile_commands_debug.json - Specify build dir and output file

echo ========================================
echo Generate compile_commands.json
echo ========================================
echo.

REM Check if Python is available
python --version >nul 2>&1
if errorlevel 1 (
    echo Error: Python not found, please ensure Python 3.6+ is installed
    echo Download from: https://www.python.org/downloads/
    pause
    exit /b 1
)

REM Set default parameters
set BUILD_DIR=..\build_Release
set OUTPUT_FILE=..\compile_commands.json

REM Process command line arguments
if not "%1"=="" set BUILD_DIR=%1
if not "%2"=="" set OUTPUT_FILE=%2.json

echo Build directory: %BUILD_DIR%
echo Output file: %OUTPUT_FILE%
echo.

REM Check if build directory exists
if not exist "%BUILD_DIR%" (
    echo Error: Build directory '%BUILD_DIR%' does not exist!
    echo.
    echo Please build the project first:
    echo   mkdir %BUILD_DIR%
    echo   cd %BUILD_DIR%
    echo   cmake .. -G "Visual Studio 16 2019" -A x64
    echo   cmake --build . --config Release
    echo   cd ..
    echo.
    pause
    exit /b 1
)

REM Run Python script
echo Generating compile_commands.json...
python generate_compile_commands.py -b "%BUILD_DIR%" -o "%OUTPUT_FILE%"

if errorlevel 1 (
    echo.
    echo Generation failed! Please check error messages.
    pause
    exit /b 1
)

echo.
echo ========================================
echo Generation completed!
echo ========================================
echo.
echo Now you can use clang-tidy for code analysis:
echo   cd ..
echo   clang-tidy --config-file=clang-tidy\.clang-tidy src\some_file.cpp
echo.
echo Or use IDE plugins like VS Code clangd extension
echo.
pause