@echo off
setlocal
cd /d "%~dp0"
if not exist "DuckEggs.exe" (
  echo DuckEggs: DuckEggs.exe not found. 1>&2
  exit /b 2
)
start "" "%~dp0DuckEggs.exe" %*
endlocal
