<#
Fills translations/labaudio_ru.ts and labaudio_tk.ts from translations/labaudio_translations.json
(source text -> [Russian, Turkmen]). Run after lupdate has added new texts; lists texts without
a translation so they can be added to the JSON file.

  lupdate -no-obsolete -locations none plugins/labaudio/*.cpp plugins/labaudio/*.h plugins/labaudio/*.ui \
          plugins/labaudiocalls/*.cpp plugins/labaudiocalls/*.h -ts translations/labaudio_ru.ts translations/labaudio_tk.ts
  powershell -ExecutionPolicy Bypass -File contrib/labaudio/update-translations.ps1
#>
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$table = Get-Content -Raw -Encoding UTF8 (Join-Path $root 'translations/labaudio_translations.json') | ConvertFrom-Json
$languages = @( @{ File = 'labaudio_ru.ts'; Index = 0; Code = 'ru' }, @{ File = 'labaudio_tk.ts'; Index = 1; Code = 'tk' } )
$missing = 0

foreach ($language in $languages) {
	$path = Join-Path $root "translations/$($language.File)"
	$xml = New-Object System.Xml.XmlDocument
	$xml.PreserveWhitespace = $true
	$xml.Load($path)
	$xml.DocumentElement.SetAttribute('language', $language.Code)
	foreach ($message in $xml.SelectNodes('//message')) {
		$source = $message.SelectSingleNode('source').InnerText
		$translation = $message.SelectSingleNode('translation')
		$entry = $table.PSObject.Properties[$source]
		if ($null -eq $entry) {
			Write-Host "missing ($($language.Code)): $source" -ForegroundColor Yellow
			$missing++
			continue
		}
		$translation.InnerText = $entry.Value[$language.Index]
		$translation.RemoveAttribute('type')
	}
	$settings = New-Object System.Xml.XmlWriterSettings
	$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
	$writer = [System.Xml.XmlWriter]::Create($path, $settings)
	$xml.Save($writer)
	$writer.Close()
	Write-Host "updated $($language.File)"
}
if ($missing -gt 0) { Write-Host "$missing texts without translation" -ForegroundColor Yellow; exit 1 }
