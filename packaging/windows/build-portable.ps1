param(
    [string]$BuildDirectory = "",
    [string]$DistDirectory = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $RepoRoot "build/package-windows"
}
if ([string]::IsNullOrWhiteSpace($DistDirectory)) {
    $DistDirectory = Join-Path $RepoRoot "dist"
}

New-Item -ItemType Directory -Force -Path $DistDirectory | Out-Null
Get-ChildItem $DistDirectory -Filter "ForeverTAS-*-windows-*.zip*" |
    Remove-Item -Force

cmake -S $RepoRoot -B $BuildDirectory -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DBUILD_TESTING=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }

cmake --build $BuildDirectory --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

cpack --config (Join-Path $BuildDirectory "CPackConfig.cmake") `
    -G ZIP -B $DistDirectory
if ($LASTEXITCODE -ne 0) { throw "CPack failed" }

$Artifacts = @(Get-ChildItem $DistDirectory -Filter "ForeverTAS-*-windows-*.zip")
if ($Artifacts.Count -ne 1) {
    throw "Expected one Windows ZIP, found $($Artifacts.Count)"
}

$Artifact = $Artifacts[0]
$Hash = Get-FileHash -Algorithm SHA256 $Artifact.FullName
"$($Hash.Hash.ToLower())  $($Artifact.Name)" |
    Set-Content -NoNewline "$($Artifact.FullName).sha256"
Write-Host "Created $($Artifact.FullName)"
