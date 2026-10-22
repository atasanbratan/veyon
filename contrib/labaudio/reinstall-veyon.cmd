@echo off
rem Uninstalls the previous Veyon and installs the newest installer from this folder.
rem Usage: reinstall-veyon.cmd [-NoMaster] [-ClearConfig]
net session >nul 2>&1
if errorlevel 1 (
	if "%~1"=="" (
		powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
	) else (
		powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -ArgumentList '%*' -Verb RunAs"
	)
	exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0reinstall-veyon.ps1" %*
echo.
pause
