param([switch]$Force)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$manifest = Join-Path $PSScriptRoot 'plugin.xml'
if ((Test-Path $manifest) -and -not $Force) { Write-Host "Native plugin manifest found; this exe loads ai_plugins/plugin.xml directly. Nothing to install."; exit 0 }
$bridge = Join-Path $root 'ai_assistant.py'
if (-not (Test-Path $bridge)) { throw "ai_assistant.py not found next to ai_plugins" }
$text = [IO.File]::ReadAllText($bridge, [Text.Encoding]::UTF8)
if ($text.Contains('from ai_plugins import install_plugin')) {
    Write-Host "Plugin hook is already present. Nothing to do."
    exit 0
}
$marker = "def main():"
$pos = $text.IndexOf($marker)
if ($pos -lt 0) { throw "Could not find 'def main():' in ai_assistant.py" }
$backup = Join-Path $PSScriptRoot 'backup\ai_assistant.py.before-install.bak'
New-Item -ItemType Directory -Force (Split-Path -Parent $backup) | Out-Null
if (-not (Test-Path $backup)) { Copy-Item $bridge $backup -Force }
$hook = @'
# --- Optional AI drawing plugin -------------------------------------------------
# Loaded from ai_plugins/ next to this file.  If the folder is missing or the
# plugin fails to import, the stock assistant above remains fully usable.
try:
    from ai_plugins import install_plugin as _install_ai_drawing_plugin
    _install_ai_drawing_plugin(globals())
except Exception as _ai_drawing_plugin_error:
    _AI_DRAW_PLUGIN_ERROR = str(_ai_drawing_plugin_error)
# --------------------------------------------------------------------------------


'@
$text = $text.Insert($pos, $hook)
[IO.File]::WriteAllText($bridge, $text, [Text.UTF8Encoding]::new($false))
Write-Host "Plugin hook installed. Restart Molecule Studio."
