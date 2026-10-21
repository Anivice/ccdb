@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 99
cd /d "C:\Users\Anivice Ives\Documents\CLionProjects\ccdb\src\out\build\windows-msvc-Debug"
"C:\PROGRA~1\MICROS~4\18\COMMUN~1\Common7\IDE\COMMON~1\MICROS~1\CMake\Ninja\ninja.exe" %*
exit /b %ERRORLEVEL%

