@echo off
setlocal
set "CCDB_CONFIG=%~1"
if not defined CCDB_CONFIG set "CCDB_CONFIG=Debug"
if /I not "%CCDB_CONFIG%"=="Debug" if /I not "%CCDB_CONFIG%"=="Release" if /I not "%CCDB_CONFIG%"=="RelWithDebInfo" exit /b 2
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CCDB_VS=%%i"
if not defined CCDB_VS (
    echo Cannot find Visual Studio C++ tools.
    exit /b 1
)
call "%CCDB_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
set "CCDB_CMAKE=%CCDB_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "PATH=%CCDB_VS%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
"%CCDB_CMAKE%" -S "%~dp0.." -B "%~dp0..\out\build\windows-msvc-%CCDB_CONFIG%" -G Ninja -DCMAKE_BUILD_TYPE=%CCDB_CONFIG%
if errorlevel 1 exit /b %errorlevel%
"%CCDB_CMAKE%" --build "%~dp0..\out\build\windows-msvc-%CCDB_CONFIG%" --parallel 4
exit /b %errorlevel%
