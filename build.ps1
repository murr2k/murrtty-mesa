<#
.SYNOPSIS
Builds Mesa's llvmpipe (opengl32.dll and libgallium_wgl.dll) for Windows, for
murrtty's software OpenGL fallback. Run from a Visual Studio developer
environment for the target (vcvarsall x64 or arm64) on a machine of that
architecture; GitHub's windows-2025 and windows-11-arm runners do it in
.github/workflows/build.yml.

.DESCRIPTION
Downloads Mesa's source and LLVM's official Windows toolchain at the versions
and SHA-256s pinned in versions.json, refuses either if its hash differs,
builds with Meson and MSVC (static C runtime, LLVM linked statically, every
driver and API but llvmpipe and desktop OpenGL left out), and puts the two
DLLs, the license texts and BUILDINFO.txt in -Out.
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Arch,
    [string]$Work = (Join-Path $PSScriptRoot 'work'),
    [string]$Out = (Join-Path $PSScriptRoot 'out')
)

$ErrorActionPreference = 'Stop'
$versions = Get-Content (Join-Path $PSScriptRoot 'versions.json') -Raw | ConvertFrom-Json
$llvmTriple = @{ x64 = 'x86_64-pc-windows-msvc'; arm64 = 'aarch64-pc-windows-msvc' }[$Arch]

function Get-Pinned([string]$Url, [string]$Sha256, [string]$File) {
    if (-not (Test-Path $File)) {
        Write-Host "downloading $Url"
        Invoke-WebRequest -Uri $Url -OutFile $File
    }
    $have = (Get-FileHash $File -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($have -ne $Sha256.ToLowerInvariant()) { throw "$File has SHA-256 $have, not the pinned $Sha256" }
    Write-Host "verified $File ($have)"
}

New-Item -ItemType Directory -Force $Work, $Out | Out-Null

# LLVM: only what Mesa links against and asks llvm-config about.
$llvmName = "clang+llvm-$($versions.llvm.version)-$llvmTriple"
$llvmArchive = Join-Path $Work "$llvmName.tar.xz"
Get-Pinned "https://github.com/llvm/llvm-project/releases/download/llvmorg-$($versions.llvm.version)/$llvmName.tar.xz" $versions.llvm.sha256.$Arch $llvmArchive
$llvm = Join-Path $Work $llvmName
if (-not (Test-Path (Join-Path $llvm 'bin\llvm-config.exe'))) {
    & "$env:SystemRoot\System32\tar.exe" -xf $llvmArchive -C $Work "$llvmName/bin/llvm-config.exe" "$llvmName/lib/*.lib" "$llvmName/include/*"
    if ($LASTEXITCODE -ne 0) { throw "extracting LLVM failed ($LASTEXITCODE)" }
}
$llvmConfig = Join-Path $llvm 'bin\llvm-config.exe'
$llvmVersion = (& $llvmConfig --version).Trim()
$rtti = if ((& $llvmConfig --has-rtti).Trim() -eq 'YES') { 'true' } else { 'false' }
$systemLibs = (& $llvmConfig --link-static --system-libs | Out-String).Trim()
Write-Host "LLVM $llvmVersion, RTTI $rtti, system libraries: $systemLibs"
Write-Host "LLVM lib folder holds: $(((Get-ChildItem (Join-Path $llvm 'lib') -Filter *.lib | Where-Object { $_.Name -notmatch '^(LLVM|clang|lld|LLD|Polly|MLIR|flang|Fortran)' }).Name) -join ', ')"

# Mesa.
$mesaName = "mesa-$($versions.mesa.version)"
$mesaArchive = Join-Path $Work "$mesaName.tar.xz"
Get-Pinned "https://archive.mesa3d.org/$mesaName.tar.xz" $versions.mesa.sha256 $mesaArchive
$mesa = Join-Path $Work $mesaName
if (-not (Test-Path (Join-Path $mesa 'meson.build'))) {
    & "$env:SystemRoot\System32\tar.exe" -xf $mesaArchive -C $Work
    if ($LASTEXITCODE -ne 0) { throw "extracting Mesa failed ($LASTEXITCODE)" }
}

$native = Join-Path $Work "native-$Arch.ini"
@"
[binaries]
llvm-config = '$($llvmConfig.Replace('\', '/'))'
"@ | Set-Content $native -Encoding ascii

$options = @(
    '--buildtype=release', '-Db_ndebug=true', '-Db_vscrt=mt', "-Dcpp_rtti=$rtti",
    '-Dplatforms=windows', '-Dgallium-drivers=llvmpipe', '-Dvulkan-drivers=[]',
    '-Dopengl=true', '-Dgles1=disabled', '-Dgles2=disabled', '-Degl=disabled', '-Dglx=disabled', '-Dglvnd=disabled',
    '-Dllvm=enabled', '-Dshared-llvm=disabled', '-Ddraw-use-llvm=true',
    '-Dgallium-va=disabled', '-Dgallium-mediafoundation=disabled', '-Dgallium-d3d10umd=false',
    '-Dgallium-rusticl=false', '-Dgallium-d3d12-video=disabled', '-Dgallium-d3d12-graphics=disabled',
    '-Dvideo-codecs=[]', '-Dmicrosoft-clc=disabled', '-Dspirv-to-dxil=false', '-Dspirv-tools=disabled',
    '-Dzlib=disabled', '-Dzstd=disabled', '-Dshader-cache=disabled', '-Dexpat=disabled', '-Dxmlconfig=disabled',
    '-Dvalgrind=disabled', '-Dlibunwind=disabled', '-Dlmsensors=disabled', '-Dbuild-tests=false', '-Dtools=[]',
    '-Dperfetto=false', '-Dteflon=false', '-Dgallium-extra-hud=false'
)
$build = Join-Path $Work "build-$Arch"
$prefix = Join-Path $Work "install-$Arch"
if (Test-Path $build) { Remove-Item $build -Recurse -Force }
meson setup $build $mesa --native-file $native --prefix $prefix @options
if ($LASTEXITCODE -ne 0) { throw "meson setup failed ($LASTEXITCODE)" }
meson compile -C $build
if ($LASTEXITCODE -ne 0) { throw "meson compile failed ($LASTEXITCODE)" }
meson install -C $build
if ($LASTEXITCODE -ne 0) { throw "meson install failed ($LASTEXITCODE)" }

# The two DLLs, wherever install put them (bin\ for DLLs).
foreach ($dll in 'opengl32.dll', 'libgallium_wgl.dll') {
    $found = Get-ChildItem $prefix -Recurse -Filter $dll | Select-Object -First 1
    if (-not $found) { throw "the install has no $dll" }
    Copy-Item $found.FullName $Out -Force
}
$licenses = Join-Path $Out 'licenses'
New-Item -ItemType Directory -Force $licenses | Out-Null
Copy-Item (Join-Path $mesa 'docs\license.rst') (Join-Path $licenses 'mesa-license.rst') -Force
# LLVM's archive carries no license file: this repository keeps the text
# from its release tag (llvm/LICENSE.TXT at llvmorg-<version>).
Copy-Item (Join-Path $PSScriptRoot 'licenses\llvm-LICENSE.TXT') (Join-Path $licenses 'llvm-LICENSE.TXT') -Force

$msvc = (& cl.exe 2>&1 | Select-Object -First 1).ToString().Trim()
$info = @(
    "Mesa $($versions.mesa.version) llvmpipe for Windows $Arch, built for murrtty's software OpenGL fallback.",
    "Mesa source: https://archive.mesa3d.org/$mesaName.tar.xz (SHA-256 $($versions.mesa.sha256))",
    "LLVM: $llvmName.tar.xz from https://github.com/llvm/llvm-project/releases/tag/llvmorg-$($versions.llvm.version) (SHA-256 $($versions.llvm.sha256.$Arch)), linked statically ($llvmVersion, RTTI $rtti)",
    "Compiler: $msvc",
    "Meson $((meson --version).Trim()) options: $($options -join ' ')"
)
if ($env:GITHUB_SHA) { $info += "Built by $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID from commit $env:GITHUB_SHA on $env:ImageOS $env:ImageVersion" }
$info | Set-Content (Join-Path $Out 'BUILDINFO.txt') -Encoding utf8
Get-ChildItem $Out -Recurse -File | ForEach-Object { "{0,12} {1}" -f $_.Length, $_.FullName.Substring($Out.Length + 1) }
