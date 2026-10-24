@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Launch-Baseline.ps1" -Variant ConditionalFix
if errorlevel 1 pause
