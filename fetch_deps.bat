@echo off
REM Fetch gumbo-parser dependency for Caracal Pro
setlocal

set GUMBO_DIR=%~dp0components\gumbo
set GUMBO_SRC=%GUMBO_DIR%\src
set GUMBO_INC=%GUMBO_DIR%\include
set GUMBO_REPO=https://github.com/google/gumbo-parser.git
set GUMBO_BRANCH=master

echo ============================================
echo  Fetching gumbo-parser for Caracal Pro
echo ============================================

where git >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: git not found. Please install git and add to PATH.
    exit /b 1
)

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

if not exist "%GUMBO_SRC%" mkdir "%GUMBO_SRC%"
copy /Y "%TEMP_DIR%\src\*.c" "%GUMBO_SRC%\"
copy /Y "%TEMP_DIR%\src\*.h" "%GUMBO_SRC%\"

if not exist "%GUMBO_INC%" mkdir "%GUMBO_INC%"
copy /Y "%TEMP_DIR%\src\gumbo.h" "%GUMBO_INC%\"

echo Cleaning up...
rmdir /S /Q "%TEMP_DIR%"

echo ============================================
echo  gumbo-parser fetched successfully!
echo ============================================

endlocal
