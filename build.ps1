<#
.SYNOPSIS
Builds Mesa's llvmpipe (opengl32.dll and libgallium_wgl.dll) for Windows, for
murrtty's software OpenGL fallback. Run from a Visual Studio developer
environment for the target (vcvarsall x64 or arm64, with the MSVC toolset
versions.json pins) on a machine of that architecture; GitHub's
windows-2025 and windows-11-arm runners do it in .github/workflows/build.yml.

.DESCRIPTION
Downloads Mesa's and LLVM's source releases at the versions and SHA-256s
pinned in versions.json, refuses either if its hash differs, builds LLVM's
libraries (the target's architecture only) and then Mesa with Meson, both
with that MSVC toolset and a static C runtime, LLVM linked statically and
every driver and API but llvmpipe and desktop OpenGL left out, and puts the
two DLLs, the license texts and BUILDINFO.txt in -Out.

LLVM is built here rather than taken from LLVM's prebuilt Windows release:
that release's libraries are compiled against a newer MSVC C++ library than
the toolset Mesa is built with, and linked into Mesa they corrupted the heap
(any program whose fragment shader reads gl_FragCoord.y), or failed to link
(__std_find_trivial_8 unresolved on ARM64).
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('x64', 'arm64')][string]$Arch,
    [string]$Work = (Join-Path $PSScriptRoot 'work'),
    [string]$Out = (Join-Path $PSScriptRoot 'out'),
    # A diagnostic build: AddressSanitizer, with debug information, never
    # released (the workflow's diagnose run).
    [switch]$Sanitize
)

$ErrorActionPreference = 'Stop'
$versions = Get-Content (Join-Path $PSScriptRoot 'versions.json') -Raw | ConvertFrom-Json

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

# LLVM: its libraries and llvm-config, used from the build tree.
$llvmSrcName = "llvm-project-$($versions.llvm.version).src"
$llvmArchive = Join-Path $Work "$llvmSrcName.tar.xz"
Get-Pinned "https://github.com/llvm/llvm-project/releases/download/llvmorg-$($versions.llvm.version)/$llvmSrcName.tar.xz" $versions.llvm.sha256 $llvmArchive
$llvmSrc = Join-Path $Work $llvmSrcName
if (-not (Test-Path (Join-Path $llvmSrc 'llvm\CMakeLists.txt'))) {
    # What LLVM's own build reads (its CMake looks for libc's folder too).
    & "$env:SystemRoot\System32\tar.exe" -xf $llvmArchive -C $Work "$llvmSrcName/llvm" "$llvmSrcName/cmake" "$llvmSrcName/third-party" "$llvmSrcName/libc"
    if ($LASTEXITCODE -ne 0) { throw "extracting LLVM failed ($LASTEXITCODE)" }
}
$llvm = Join-Path $Work "llvm-build-$Arch"
$llvmTarget = @{ x64 = 'X86'; arm64 = 'AArch64' }[$Arch]
if (-not (Test-Path (Join-Path $llvm 'bin\llvm-config.exe'))) {
    # No DIA SDK: the pinned toolset has no ATL, and Mesa needs no PDB reader.
    cmake -G Ninja -S (Join-Path $llvmSrc 'llvm') -B $llvm -Wno-dev `
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded `
        "-DLLVM_TARGETS_TO_BUILD=$llvmTarget" -DLLVM_ENABLE_PROJECTS= `
        -DLLVM_ENABLE_ASSERTIONS=OFF -DLLVM_ENABLE_RTTI=OFF `
        -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF `
        -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_LIBPFM=OFF -DLLVM_ENABLE_Z3_SOLVER=OFF `
        -DLLVM_ENABLE_BACKTRACES=OFF -DLLVM_ENABLE_UNWIND_TABLES=OFF -DLLVM_ENABLE_CRASH_OVERRIDES=OFF `
        -DLLVM_ENABLE_BINDINGS=OFF -DLLVM_ENABLE_PLUGINS=OFF -DLLVM_OPTIMIZED_TABLEGEN=ON -DLLVM_ENABLE_DIA_SDK=OFF `
        -DLLVM_BUILD_TOOLS=OFF -DLLVM_BUILD_UTILS=OFF -DLLVM_INCLUDE_UTILS=OFF `
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF -DLLVM_INCLUDE_DOCS=OFF
    if ($LASTEXITCODE -ne 0) { throw "configuring LLVM failed ($LASTEXITCODE)" }
    ninja -C $llvm
    if ($LASTEXITCODE -ne 0) { throw "building LLVM failed ($LASTEXITCODE)" }
    ninja -C $llvm llvm-config
    if ($LASTEXITCODE -ne 0) { throw "building llvm-config failed ($LASTEXITCODE)" }
}
$llvmConfig = Join-Path $llvm 'bin\llvm-config.exe'
$llvmVersion = (& $llvmConfig --version).Trim()
$rtti = if ((& $llvmConfig --has-rtti).Trim() -eq 'YES') { 'true' } else { 'false' }
$systemLibs = (& $llvmConfig --link-static --system-libs | Out-String).Trim()
Write-Host "LLVM $llvmVersion for $llvmTarget, RTTI $rtti, system libraries: $systemLibs"

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
@('[binaries]', "llvm-config = ['$($llvmConfig.Replace('\', '/'))']") -join "`n" | Set-Content $native -Encoding ascii

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
if ($Sanitize) {
    $options = @($options | Where-Object { $_ -ne '--buildtype=release' }) + @(
        '--buildtype=debugoptimized', '-Db_sanitize=address', '-Dc_link_args=/INCREMENTAL:NO', '-Dcpp_link_args=/INCREMENTAL:NO',
        # LLVM's libraries are built without the C++ library's ASan
        # annotations; linking them to annotated code is a mismatch (LNK2038).
        '-Dcpp_args=/D_DISABLE_VECTOR_ANNOTATION /D_DISABLE_STRING_ANNOTATION'
    )
}
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
# The license texts license.rst points to (each source file's SPDX
# identifier names one of them).
$mesaLicenses = Join-Path $licenses 'mesa'
if (Test-Path $mesaLicenses) { Remove-Item $mesaLicenses -Recurse -Force }
Copy-Item (Join-Path $mesa 'licenses') $mesaLicenses -Recurse -Force
Copy-Item (Join-Path $llvmSrc 'llvm\LICENSE.TXT') (Join-Path $licenses 'llvm-LICENSE.TXT') -Force

$msvc = (& cl.exe 2>&1 | Select-Object -First 1).ToString().Trim()
$info = @(
    "Mesa $($versions.mesa.version) llvmpipe for Windows $Arch, built for murrtty's software OpenGL fallback.",
    "Mesa source: https://archive.mesa3d.org/$mesaName.tar.xz (SHA-256 $($versions.mesa.sha256))",
    "LLVM: $llvmSrcName.tar.xz from https://github.com/llvm/llvm-project/releases/tag/llvmorg-$($versions.llvm.version) (SHA-256 $($versions.llvm.sha256)), built here for $llvmTarget and linked statically ($llvmVersion, RTTI $rtti)",
    "Compiler: $msvc (MSVC toolset $env:VCToolsVersion), static C runtime",
    "Meson $((meson --version).Trim()) options: $($options -join ' ')"
)
if ($env:GITHUB_SHA) { $info += "Built by $env:GITHUB_SERVER_URL/$env:GITHUB_REPOSITORY/actions/runs/$env:GITHUB_RUN_ID from commit $env:GITHUB_SHA on $env:ImageOS $env:ImageVersion" }
$info | Set-Content (Join-Path $Out 'BUILDINFO.txt') -Encoding utf8
Get-ChildItem $Out -Recurse -File | ForEach-Object { "{0,12} {1}" -f $_.Length, $_.FullName.Substring($Out.Length + 1) }
