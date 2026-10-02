@echo off
rem ---------------------------------------------------------------------------
rem  Run build.ps1 with the script execution policy bypassed for this process.
rem  Windows client defaults to "Restricted", which refuses to load any .ps1
rem  ("cannot be loaded because running scripts is disabled on this system").
rem  Usage:  build.cmd [build.ps1 arguments]
rem ---------------------------------------------------------------------------
setlocal
chcp 65001 >nul

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build.ps1" %*
set "code=%ERRORLEVEL%"

rem Double-clicked from Explorer (no arguments): keep the window open.
if "%~1"=="" pause

exit /b %code%
