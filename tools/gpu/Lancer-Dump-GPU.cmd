@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-f8-capture.ps1" %*
if errorlevel 1 pause
