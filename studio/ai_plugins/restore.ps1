param([switch]$Docs)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$bridge = Join-Path $root 'ai_assistant.py'
$backup = Join-Path $PSScriptRoot 'backup\ai_assistant.py.0.9.3.bak'
if (-not (Test-Path $backup)) { throw "Backup not found: $backup" }
Copy-Item $backup $bridge -Force
if ($Docs) {
    $docBackup = Join-Path $PSScriptRoot 'backup\AI 使用说明.md.0.9.3.bak'
    if (Test-Path $docBackup) { Copy-Item $docBackup (Join-Path $root 'AI 使用说明.md') -Force }
}
Write-Host "Restored ai_assistant.py from the 0.9.3 backup. Restart Molecule Studio."
