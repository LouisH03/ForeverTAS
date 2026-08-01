$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($env:RUNNER_TOOL_CACHE)) {
    throw "RUNNER_TOOL_CACHE is required"
}
if ([string]::IsNullOrWhiteSpace($env:VCPKG_COMMIT)) {
    throw "VCPKG_COMMIT is required"
}

$CacheRoot = Join-Path $env:RUNNER_TOOL_CACHE "forevertas"
$VcpkgRoot = Join-Path $CacheRoot "vcpkg"
$BinaryCache = Join-Path $CacheRoot "vcpkg-binary-cache"
$SccacheDirectory = Join-Path $CacheRoot "sccache-windows"
$SearchCache = Join-Path $CacheRoot "cuda-search-windows"
New-Item -ItemType Directory -Force `
    -Path $CacheRoot, $BinaryCache, $SccacheDirectory, $SearchCache | Out-Null

if (-not (Test-Path (Join-Path $VcpkgRoot ".git"))) {
    git clone https://github.com/microsoft/vcpkg.git $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "Failed to clone vcpkg" }
}

git -C $VcpkgRoot fetch --depth 1 origin $env:VCPKG_COMMIT
if ($LASTEXITCODE -ne 0) { throw "Failed to fetch pinned vcpkg commit" }
git -C $VcpkgRoot checkout --detach --force $env:VCPKG_COMMIT
if ($LASTEXITCODE -ne 0) { throw "Failed to check out pinned vcpkg commit" }

$Vcpkg = Join-Path $VcpkgRoot "vcpkg.exe"
if (-not (Test-Path $Vcpkg)) {
    & (Join-Path $VcpkgRoot "bootstrap-vcpkg.bat") -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "Failed to bootstrap vcpkg" }
}

$env:VCPKG_BINARY_SOURCES = "clear;files,$BinaryCache,readwrite"
& $Vcpkg install --triplet x64-windows openssl zlib
if ($LASTEXITCODE -ne 0) { throw "Failed to install Windows dependencies" }

"VCPKG_INSTALLATION_ROOT=$VcpkgRoot" >> $env:GITHUB_ENV
"VCPKG_BINARY_SOURCES=$env:VCPKG_BINARY_SOURCES" >> $env:GITHUB_ENV
"SCCACHE_DIR=$SccacheDirectory" >> $env:GITHUB_ENV
"SCCACHE_CACHE_SIZE=50G" >> $env:GITHUB_ENV
"FOREVERTAS_WINDOWS_SEARCH_CACHE=$SearchCache" >> $env:GITHUB_ENV
