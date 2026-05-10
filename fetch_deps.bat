@echo off
REM Fetch gumbo-parser dependency for ESP32-S3 browser project
REM This script downloads the gumbo-parser source into components/gumbo/

setlocal

set GUMBO_DIR=%~dp0components\gumbo
set GUMBO_SRC=%GUMBO_DIR%\src
set GUMBO_INC=%GUMBO_DIR%\include
set GUMBO_REPO=https://github.com/google/gumbo-parser.git
set GUMBO_BRANCH=master

echo ============================================
echo  Fetching gumbo-parser for ESP32-S3 Browser
echo ============================================

REM Check if git is available
where git >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: git not found. Please install git and add to PATH.
    exit /b 1
)

REM Create temp directory for clone
set TEMP_DIR=%GUMBO_DIR%\_gumbo_tmp

if exist "%GUMBO_SRC%\parser.c" (
    echo gumbo-parser source already exists, skipping download.
    exit /b 0
)

echo Cloning gumbo-parser repository...
git clone --depth 1 --branch %GUMBO_BRANCH% %GUMBO_REPO% "%TEMP_DIR%"

if %ERRORLEVEL% neq 0 (
    echo ERROR: Failed to clone gumbo-parser.
    exit /b 1
)

REM Copy source files
echo Copying source files...
if not exist "%GUMBO_SRC%" mkdir "%GUMBO_SRC%"
copy /Y "%TEMP_DIR%\src\*.c" "%GUMBO_SRC%\"
copy /Y "%TEMP_DIR%\src\*.h" "%GUMBO_SRC%\"

REM Copy public header
if not exist "%GUMBO_INC%" mkdir "%GUMBO_INC%"
copy /Y "%TEMP_DIR%\src\gumbo.h" "%GUMBO_INC%\"

REM Cleanup temp
echo Cleaning up...
rmdir /S /Q "%TEMP_DIR%"

echo ============================================
echo  gumbo-parser fetched successfully!
echo ============================================

endlocal
