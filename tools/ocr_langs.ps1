Add-Type -AssemblyName System.Runtime.WindowsRuntime
[Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime] | Out-Null
$l = [Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages
if ($l.Count -eq 0) { Write-Output "(no OCR languages installed)" }
$l | ForEach-Object { $_.LanguageTag + "  " + $_.DisplayName }
