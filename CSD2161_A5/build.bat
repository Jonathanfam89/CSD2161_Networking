@echo off
setlocal

set MSBUILD="%ProgramFiles%\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"
if not exist %MSBUILD% (
    set MSBUILD="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
)
if not exist %MSBUILD% (
    echo ERROR: MSBuild not found. Install Visual Studio 2022 or Build Tools.
    exit /b 1
)

echo ===============================================
echo       Choose build configuration:
echo ===============================================
echo   1. Release
echo   2. Debug
echo ===============================================
set /p CHOICE="Enter choice (1/2): "

set CONFIG=Release
if "%CHOICE%"=="2" set CONFIG=Debug

echo.
echo Building %CONFIG% x64...
%MSBUILD% CSD2161_A5.sln -p:Configuration=%CONFIG% -p:Platform=x64 -verbosity:minimal
if %errorlevel% neq 0 (
    echo Build FAILED.
    exit /b 1
)

echo Build succeeded: x64\%CONFIG%\
