param([string]$AppDir = $PSScriptRoot)
$ErrorActionPreference = 'Stop'
$log = Join-Path $PSScriptRoot 'OCSR-diagnostic.txt'
$lines = [System.Collections.Generic.List[string]]::new()
function Note([string]$value) { $lines.Add($value); Write-Host $value }
function RunCheck([string]$exe, [string]$arguments, [string]$inputText = '') {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $exe
    $info.Arguments = $arguments
    $info.WorkingDirectory = $AppDir
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.RedirectStandardInput = $true
    $info.EnvironmentVariables['PYTHONUTF8'] = '1'
    $info.EnvironmentVariables.Remove('PYTHONHOME')
    $info.EnvironmentVariables.Remove('PYTHONPATH')
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $info
    [void]$process.Start()
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    if ($inputText) { $process.StandardInput.WriteLine($inputText) }
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(120000)) {
        $process.Kill()
        $process.WaitForExit()
        Note 'TIMEOUT after 120 seconds'
    }
    Note ('Exit code: ' + $process.ExitCode)
    Note ('stdout: ' + $out.Result)
    Note ('stderr: ' + $err.Result)
    $process.Dispose()
}
try {
    $AppDir = [IO.Path]::GetFullPath($AppDir)
    Note ('MoleculeStudio OCSR diagnostic / ' + [DateTime]::Now.ToString('s'))
    Note ('App directory: ' + $AppDir)
    Note ('64-bit Windows: ' + [Environment]::Is64BitOperatingSystem)
    foreach ($dll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        Note ($dll + ' in System32: ' + (Test-Path -LiteralPath (Join-Path $env:SystemRoot "System32\$dll")))
    }
    $runtime = Join-Path $AppDir 'ocsr_runtime'
    $python = Join-Path $runtime 'venv\Scripts\python.exe'
    $base = Join-Path $runtime 'python\cpython-3.10-windows-x86_64-none'
    $cfg = Join-Path $runtime 'venv\pyvenv.cfg'
    $service = Join-Path $AppDir 'ocsr_service.py'
    foreach ($file in @($python,$cfg,$service,(Join-Path $base 'python.exe'),(Join-Path $base 'Lib\encodings\__init__.py'),(Join-Path $runtime 'models\swin_base_char_aux_1m680k.pth'))) {
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing required file: $file. Merge Core and OCSR into the SAME MoleculeStudio directory." }
        Note ('Found: ' + $file)
    }
    $text = [IO.File]::ReadAllText($cfg)
    $replacement = 'home = ' + $base
    $updated = [regex]::Replace($text, '(?m)^\s*home\s*=[^\r\n]*', [System.Text.RegularExpressions.MatchEvaluator]{ param($m) $replacement })
    if ($updated -eq $text -and $text -notmatch '(?m)^\s*home\s*=') { $updated = $replacement + "`r`n" + $text }
    if ($updated -ne $text) {
        $backup = $cfg + '.diagnostic-backup'
        if (-not (Test-Path -LiteralPath $backup)) { [IO.File]::Copy($cfg,$backup) }
        [IO.File]::WriteAllText($cfg,$updated,[Text.UTF8Encoding]::new($false))
        Note 'Updated portable Python home path (original configuration backed up).'
    }
    Note 'Checking Python...'
    RunCheck $python '-I -c "import sys; print(sys.version); print(sys.executable)"'
    Note 'Checking recognition dependencies...'
    RunCheck $python '-I -c "import torch, cv2, molscribe, pypdfium2; print(torch.__version__); print(''IMPORTS_OK'')"'
    Note 'Checking actual worker status...'
    RunCheck $python ('"' + $service + '" --serve') '<request id="1" op="status"/>'
    Note 'Done. Send OCSR-diagnostic.txt to the maintainer. It contains local paths; no API keys are collected.'
} catch { Note ('ERROR: ' + $_.Exception.Message) }
finally {
    [IO.File]::WriteAllLines($log,$lines,[Text.UTF8Encoding]::new($false))
    Write-Host ('Saved: ' + $log)
}
