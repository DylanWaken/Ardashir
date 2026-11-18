param(
    [string]$ProjectRoot = 'D:/UnrealProjects/ArdashirUnrealTest',
    [string]$EngineRoot = 'D:/UnrealEngine',
    [string]$CudaArchitecture = 'sm_120',
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$PluginRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$TemplateRoot = Join-Path $PSScriptRoot 'ProjectTemplate'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
if (-not $env:CUDA_PATH) { throw 'CUDA_PATH must identify the installed CUDA toolkit.' }
$Nvcc = Join-Path $env:CUDA_PATH 'bin/nvcc.exe'
if (-not (Test-Path -LiteralPath $Nvcc)) { throw "CUDA compiler not found: $Nvcc" }
if ($CudaArchitecture -notmatch '^sm_([0-9]+)$') { throw 'Select a native CUDA architecture, for example sm_120.' }
$ArchitectureNumber = $Matches[1]
$ProjectFile = Join-Path $ProjectRoot 'ArdashirUnrealTest.uproject'
if ((Test-Path -LiteralPath $ProjectRoot) -and
    -not (Test-Path -LiteralPath $ProjectFile) -and
    (Get-ChildItem -LiteralPath $ProjectRoot -Force | Select-Object -First 1))
{
    throw "The destination is not an ArdashirUnrealTest fixture: $ProjectRoot"
}
New-Item -ItemType Directory -Path $ProjectRoot -Force | Out-Null
Get-ChildItem -LiteralPath $TemplateRoot -File -Recurse -Force | ForEach-Object {
    $RelativePath = [IO.Path]::GetRelativePath($TemplateRoot, $_.FullName)
    # Keep rules as .cs.in in the plugin so UBT cannot discover duplicate project rules.
    if ($RelativePath.EndsWith('.cs.in')) { $RelativePath = $RelativePath.Substring(0, $RelativePath.Length - 3) }
    $DestinationPath = Join-Path $ProjectRoot $RelativePath
    New-Item -ItemType Directory -Path (Split-Path $DestinationPath -Parent) -Force | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $DestinationPath -Force
}
$Project = Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json
$Project | Add-Member -NotePropertyName 'AdditionalPluginDirectories' -NotePropertyValue @((Split-Path $PluginRoot -Parent)) -Force
$Project | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $ProjectFile -Encoding utf8
$KernelDirectory = Join-Path $ProjectRoot 'Content/ArdaCuda'
New-Item -ItemType Directory -Path $KernelDirectory -Force | Out-Null
$CompilerArguments = @()
if (-not (Get-Command 'cl.exe' -ErrorAction SilentlyContinue))
{
    $VsWhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $VsWhere)) { throw 'MSVC cl.exe and vswhere.exe are unavailable.' }
    $Compilers = @(& $VsWhere '-latest' '-products' '*' '-find' 'VC/Tools/MSVC/*/bin/Hostx64/x64/cl.exe')
    if ($Compilers.Count -eq 0) { throw 'Install the Visual Studio x64 C++ build tools before compiling CUDA.' }
    $CompilerArguments = @('--compiler-bindir', (Split-Path $Compilers[0] -Parent))
}
& $Nvcc @CompilerArguments '--cubin' "-arch=$CudaArchitecture" '--std=c++17' (Join-Path $ProjectRoot 'Cuda/ArdaPostProcess.cu') '-o' (Join-Path $KernelDirectory 'ArdaPostProcess.cubin')
if ($LASTEXITCODE -ne 0) { throw "CUDA kernel compilation failed with exit $LASTEXITCODE" }
$ArchitectureNumber | Set-Content -LiteralPath (Join-Path $KernelDirectory 'Architecture.txt') -Encoding ascii
Write-Output "Prepared $ProjectFile using $PluginRoot"
if ($Build)
{
    & (Join-Path $EngineRoot 'Engine/Build/BatchFiles/Build.bat') 'ArdashirUnrealTestEditor' 'Win64' 'Development' $ProjectFile '-WaitMutex' '-NoHotReloadFromIDE'
    if ($LASTEXITCODE -ne 0) { throw "Unreal build failed with exit $LASTEXITCODE" }
}
