<#
Uninstalls the installed Veyon and silently installs the newest veyon-*-labaudio-win64-setup.exe
from this folder. Keeps the Veyon configuration (authentication keys, locations) unless -ClearConfig.

Run as administrator, or simply double-click reinstall-veyon.cmd next to this file.

  -NoMaster      install without Veyon Master (student computers)
  -ClearConfig   also delete the Veyon configuration on uninstall
#>
param(
	[switch]$NoMaster,
	[switch]$ClearConfig
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$log = Join-Path $here 'reinstall-veyon.log'
Start-Transcript -Path $log -Append | Out-Null

function Fail($message) {
	Write-Host "ERROR: $message" -ForegroundColor Red
	Stop-Transcript | Out-Null
	exit 1
}

try {
	$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
	if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
		Fail 'Please run as administrator (use reinstall-veyon.cmd).'
	}

	$setup = Get-ChildItem $here -Filter 'veyon-*-labaudio-win64-setup.exe' |
		Sort-Object LastWriteTime -Descending | Select-Object -First 1
	if (-not $setup) { Fail "No veyon-*-labaudio-win64-setup.exe found in $here" }
	Write-Host "Installer: $($setup.Name) ($($setup.LastWriteTime))"

	# 1. Close Veyon programs so no files are locked.
	Write-Host 'Stopping Veyon...'
	Get-Process veyon-master, veyon-configurator -ErrorAction SilentlyContinue | Stop-Process -Force
	Stop-Service VeyonService -ErrorAction SilentlyContinue

	# 2. Uninstall the previous version.
	$key = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\Veyon'
	$installed = Get-ItemProperty $key -ErrorAction SilentlyContinue
	if ($installed -and $installed.UninstallString) {
		$uninstaller = $installed.UninstallString.Trim('"')
		$installDir = Split-Path -Parent $uninstaller
		Write-Host "Uninstalling Veyon $($installed.DisplayVersion) from $installDir..."
		if (Test-Path $uninstaller) {
			# _?= makes the NSIS uninstaller run in place, so we can wait for it to finish.
			$argList = @('/S')
			if ($ClearConfig) { $argList += '/ClearConfig' }
			$argList += "_?=$installDir"
			$p = Start-Process -FilePath $uninstaller -ArgumentList $argList -Wait -PassThru
			if ($p.ExitCode -ne 0) { Fail "Uninstaller exited with code $($p.ExitCode)" }
			Remove-Item $uninstaller -Force -ErrorAction SilentlyContinue
		} else {
			Write-Host "Uninstaller not found, installing over the existing files."
		}
	} else {
		Write-Host 'No installed Veyon found.'
	}

	# Leftover workers (e.g. an audio hub) would keep files and port 11500 busy.
	Get-Process veyon-* -ErrorAction SilentlyContinue | Stop-Process -Force
	Start-Sleep -Seconds 2

	# 3. Install the new version silently.
	Write-Host 'Installing...'
	$argList = @('/S')
	if ($NoMaster) { $argList += '/NoMaster' }
	$p = Start-Process -FilePath $setup.FullName -ArgumentList $argList -Wait -PassThru
	if ($p.ExitCode -ne 0) { Fail "Installer exited with code $($p.ExitCode)" }

	# 4. Check the result.
	$installed = Get-ItemProperty $key -ErrorAction SilentlyContinue
	if (-not $installed) { Fail 'Veyon is not registered after installation.' }
	$installDir = Split-Path -Parent $installed.UninstallString.Trim('"')
	$plugin = Join-Path $installDir 'plugins\labaudio.dll'
	if (-not (Test-Path $plugin)) { Fail "LabAudio plugin missing: $plugin" }
	$service = Get-Service VeyonService -ErrorAction SilentlyContinue
	$rule = Get-NetFirewallRule -DisplayName 'Veyon Lab Audio' -ErrorAction SilentlyContinue

	Write-Host ''
	Write-Host "Veyon $($installed.DisplayVersion) installed in $installDir" -ForegroundColor Green
	Write-Host "  LabAudio plugin : $((Get-Item $plugin).LastWriteTime)"
	Write-Host "  Veyon service   : $(if ($service) { $service.Status } else { 'missing' })"
	Write-Host "  Firewall rule   : $(if ($rule) { 'present' } else { 'MISSING' })"
	Stop-Transcript | Out-Null
	exit 0
}
catch {
	Fail $_.Exception.Message
}
