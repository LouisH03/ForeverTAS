param(
    [string]$BuildDirectory = "",
    [string]$DistDirectory = "",
    [string]$RuntimeDirectory = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $RepoRoot "build/package-windows"
}
if ([string]::IsNullOrWhiteSpace($DistDirectory)) {
    $DistDirectory = Join-Path $RepoRoot "dist"
}
if ([string]::IsNullOrWhiteSpace($RuntimeDirectory) -and
        -not [string]::IsNullOrWhiteSpace($env:VCPKG_INSTALLATION_ROOT)) {
    $RuntimeDirectory = Join-Path $env:VCPKG_INSTALLATION_ROOT `
        "installed/x64-windows/bin"
}
if ([string]::IsNullOrWhiteSpace($RuntimeDirectory)) {
    throw "RuntimeDirectory or VCPKG_INSTALLATION_ROOT is required"
}
if ([string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
    throw "Run this script from an MSVC developer environment"
}

New-Item -ItemType Directory -Force -Path $DistDirectory | Out-Null
Get-ChildItem $DistDirectory -Filter "ForeverTAS-*-windows-*.zip*" |
    Remove-Item -Force

cmake -S $RepoRoot -B $BuildDirectory -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    "-DFOREVERTAS_WINDOWS_RUNTIME_DIR=$RuntimeDirectory" `
    "-DFOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR=$env:VCToolsRedistDir/x64/Microsoft.VC143.CRT" `
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
& (Join-Path $PSScriptRoot "test-portable.ps1") -Archive $Artifact.FullName
Write-Host "Created $($Artifact.FullName)"
