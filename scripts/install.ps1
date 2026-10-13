<#
  RL Trainer : installation. Copie rltrainer.dll dans BakkesMod et active le chargement automatique.
  Utilisation : double-clique sur install.bat, ou
      powershell -NoProfile -ExecutionPolicy Bypass -File install.ps1 [-NoAutoload] [-Dll <chemin>]
#>
param(
  [switch]$NoAutoload,
  [string]$Dll = (Join-Path $PSScriptRoot 'rltrainer.dll')
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path $Dll)) { throw "rltrainer.dll introuvable à côté du script ($Dll)" }

# BakkesMod enregistre son dossier dans HKCU\Software\BakkesMod\AppPath : BakkesModPath
$bm = $null
try { $bm = (Get-ItemProperty 'HKCU:\Software\BakkesMod\AppPath' -Name BakkesModPath -ErrorAction Stop).BakkesModPath } catch { }
if (-not $bm) { $bm = Join-Path $env:APPDATA 'bakkesmod\bakkesmod' }
$plugins = Join-Path $bm 'plugins'
if (-not (Test-Path $plugins)) { throw "Dossier plugins de BakkesMod introuvable : $plugins (installe BakkesMod et lance-le une fois)" }

if (Get-Process -Name RocketLeague -ErrorAction SilentlyContinue) {
  Write-Warning "Rocket League est lancé. Si RL Trainer est déjà chargé, tape 'plugin unload rltrainer' dans la console F6 avant."
}
Copy-Item -LiteralPath $Dll -Destination $plugins -Force
Write-Host "rltrainer.dll copié dans $plugins"

if (-not $NoAutoload) {
  # BakkesMod exécute cfg\plugins.cfg au démarrage : une ligne 'plugin load <nom>' par plugin.
  $cfg  = Join-Path $bm 'cfg\plugins.cfg'
  $line = 'plugin load rltrainer'
  $raw  = if (Test-Path $cfg) { [IO.File]::ReadAllText($cfg) } else { '' }
  $present = $raw -split "`r?`n" | Where-Object { $_.Trim().ToLowerInvariant() -eq $line }
  if (-not $present) {
    $prefix = if ($raw.Length -gt 0 -and -not $raw.EndsWith("`n")) { "`r`n" } else { '' }
    [IO.File]::AppendAllText($cfg, "$prefix$line`r`n", [Text.Encoding]::ASCII)
    Write-Host "Ligne '$line' ajoutée à $cfg"
  }
}
Write-Host "Terminé. Dans Rocket League, appuie sur F6 et tape : plugin load rltrainer (ou relance le jeu)."
