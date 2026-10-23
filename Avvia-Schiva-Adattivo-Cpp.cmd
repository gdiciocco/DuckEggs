@echo off
setlocal
cd /d "%~dp0"
if not exist "Schiva-Adattivo-Cpp.exe" (
  echo Schiva Adattivo C++: Schiva-Adattivo-Cpp.exe not found. 1>&2
  exit /b 2
)
start "" "%~dp0Schiva-Adattivo-Cpp.exe" %*
endlocal
