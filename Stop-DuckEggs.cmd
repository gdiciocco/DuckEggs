@echo off
setlocal
cd /d "%~dp0"
if not exist "DuckEggs.exe" (
  echo DuckEggs: DuckEggs.exe not found. 1>&2
  exit /b 2
)
"%~dp0DuckEggs.exe" --stop %*
exit /b %errorlevel%
