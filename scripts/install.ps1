<#
  RL Trainer : installation en une commande.

  Méthode la plus simple (PowerShell, télécharge toujours la dernière version) :
      irm https://github.com/mateo-brl/RL-trainer/releases/latest/download/install.ps1 | iex

  Ou depuis le zip de la release : double-clique sur install.bat (utilise rltrainer.dll à côté du script).

  Options : -NoAutoload (ne pas activer le chargement automatique), -Dll <chemin> (DLL précise).
#>
param(
  [switch]$NoAutoload,
  [string]$Dll = ''
)
$ErrorActionPreference = 'Stop'
$Repo = 'mateo-brl/RL-trainer'

function Write-Step($text) { Write-Host "[RL Trainer] $text" -ForegroundColor Cyan }

# 1. La DLL : à côté du script si elle existe, sinon la dernière release GitHub.
if (-not $Dll) {
  $local = if ($PSScriptRoot) { Join-Path $PSScriptRoot 'rltrainer.dll' } else { '' }
  if ($local -and (Test-Path $local)) {
    $Dll = $local
  } else {
    $tmp = Join-Path $env:TEMP 'rltrainer-install'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $Dll = Join-Path $tmp 'rltrainer.dll'
    Write-Step 'Téléchargement de la dernière version...'
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    Invoke-WebRequest "https://github.com/$Repo/releases/latest/download/rltrainer.dll" -OutFile $Dll -UseBasicParsing
  }
}
if (-not (Test-Path $Dll)) { throw "rltrainer.dll introuvable ($Dll)" }
$bytes = [IO.File]::ReadAllBytes($Dll)
if ($bytes.Length -lt 65536 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw "Le fichier téléchargé n'est pas une DLL valide." }

# 2. Le dossier BakkesMod (registre puis %APPDATA%).
$bm = $null
try { $bm = (Get-ItemProperty 'HKCU:\Software\BakkesMod\AppPath' -Name BakkesModPath -ErrorAction Stop).BakkesModPath } catch { }
if (-not $bm) { $bm = Join-Path $env:APPDATA 'bakkesmod\bakkesmod' }
$plugins = Join-Path $bm 'plugins'
if (-not (Test-Path $plugins)) { throw "Dossier plugins de BakkesMod introuvable : $plugins. Installe BakkesMod (bakkesmod.com) et lance-le une fois." }

# 3. Rocket League doit être fermé (la DLL est verrouillée quand le plugin est chargé).
while (Get-Process -Name RocketLeague -ErrorAction SilentlyContinue) {
  Write-Warning 'Rocket League est ouvert. Ferme le jeu puis appuie sur Entrée (ou Ctrl+C pour annuler).'
  [void](Read-Host)
}

Copy-Item -LiteralPath $Dll -Destination (Join-Path $plugins 'rltrainer.dll') -Force
Write-Step "Plugin copié dans $plugins"

# 4. Chargement automatique au lancement du jeu.
if (-not $NoAutoload) {
  $cfg  = Join-Path $bm 'cfg\plugins.cfg'
  $line = 'plugin load rltrainer'
  New-Item -ItemType Directory -Force (Split-Path $cfg) | Out-Null
  $raw  = if (Test-Path $cfg) { [IO.File]::ReadAllText($cfg) } else { '' }
  $present = $raw -split "`r?`n" | Where-Object { $_.Trim().ToLowerInvariant() -eq $line }
  if (-not $present) {
    $prefix = if ($raw.Length -gt 0 -and -not $raw.EndsWith("`n")) { "`r`n" } else { '' }
    [IO.File]::AppendAllText($cfg, "$prefix$line`r`n", [Text.Encoding]::ASCII)
    Write-Step 'Chargement automatique activé.'
  }
}
Write-Host ''
Write-Step 'Installation terminée ! Lance Rocket League avec BakkesMod, puis F2 > Plugins > RL Trainer.'
