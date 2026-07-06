# Unpacks the freeware C&C downloads: archive -> ISO -> game files.
# Usage: pwsh -File tools/extract-assets.ps1
# Requires the portable 7-Zip in tools/7zip (handles zip, rar, and iso).

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sevenZip = Join-Path $PSScriptRoot '7zip\7z.exe'
$downloads = Join-Path $root 'data\downloads'
$discs = Join-Path $root 'data\discs'
$extracted = Join-Path $root 'data\extracted'

# archive file -> destination folder under data/extracted
$targets = @{
    'GDI95.zip'                = 'tiberian_dawn\gdi'
    'NOD95.zip'                = 'tiberian_dawn\nod'
    'CovertOps_ISO.zip'        = 'tiberian_dawn\covert_ops'
    'RedAlert1_AlliedDisc.rar' = 'red_alert\allied'
    'RedAlert1_SovietDisc.rar' = 'red_alert\soviet'
    'RACounterstrike.zip'      = 'red_alert\counterstrike'
    'RATheAftermath.zip'       = 'red_alert\aftermath'
}

foreach ($entry in $targets.GetEnumerator()) {
    $archive = Join-Path $downloads $entry.Key
    if (-not (Test-Path $archive)) { Write-Warning "Missing $($entry.Key), skipping"; continue }

    $discDir = Join-Path $discs ($entry.Key -replace '\.(zip|rar)$', '')
    $outDir = Join-Path $extracted $entry.Value
    if (Test-Path $outDir) { Write-Output "Already extracted: $($entry.Value)"; continue }

    Write-Output "=== $($entry.Key) ==="
    & $sevenZip x $archive -o"$discDir" -y | Select-Object -Last 2

    # If the archive contained an ISO, extract the ISO contents; otherwise the
    # archive held the game files directly.
    $isos = Get-ChildItem $discDir -Recurse -Include '*.iso', '*.img', '*.bin' -File -ErrorAction SilentlyContinue
    if ($isos) {
        foreach ($iso in $isos) {
            Write-Output "  ISO: $($iso.Name)"
            & $sevenZip x $iso.FullName -o"$outDir" -y | Select-Object -Last 2
        }
    } else {
        New-Item -ItemType Directory -Force $outDir | Out-Null
        Copy-Item "$discDir\*" $outDir -Recurse -Force
    }
}

Write-Output "`n=== Inventory of extracted game data ==="
Get-ChildItem $extracted -Recurse -Include '*.mix', '*.ini', '*.pal' -File |
    Sort-Object FullName |
    ForEach-Object { "{0,10:N0} KB  {1}" -f ($_.Length / 1KB), $_.FullName.Substring($extracted.Length + 1) }
