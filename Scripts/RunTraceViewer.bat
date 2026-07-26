@echo off
setlocal

set "SCRIPT_DIRECTORY=%~dp0"
for %%I in ("%SCRIPT_DIRECTORY%..") do set "SOURCE_DIRECTORY=%%~fI"

python "%SOURCE_DIRECTORY%\Tools\ArdaTraceViewer\app.py" %*
exit /b %errorlevel%
