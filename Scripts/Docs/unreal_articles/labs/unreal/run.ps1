param(
    [string]$EngineRoot = 'D:\UnrealEngine',
    [string]$LabRoot = "$env:LOCALAPPDATA\Temp\arda-unreal-lab",
    [ValidateSet('capture.py','capture_bunny.py','capture_gpu.py')]
    [string]$Script = 'capture.py',
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$projectRoot = Join-Path $LabRoot 'UnrealLab'
New-Item -ItemType Directory -Force (Join-Path $projectRoot 'Config') | Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'UnrealLab.uproject') -Destination $projectRoot -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'DefaultEngine.ini') -Destination (Join-Path $projectRoot 'Config\DefaultEngine.ini') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'Source') -Destination $projectRoot -Recurse -Force
$project = Join-Path $projectRoot 'UnrealLab.uproject'
if ($Build) {
    & (Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat') UnrealLabEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReload
    if ($LASTEXITCODE -ne 0) { throw 'Editor bridge build failed.' }
}
$env:ARDA_UNREAL_LAB = $LabRoot
$scriptPath = Join-Path $PSScriptRoot $Script
$arguments = '"{0}" -ExecutePythonScript="{1}" -RenderOffscreen -unattended -nosplash -NoSound -NoSourceControl -dx12 -ResX=1280 -ResY=720 -ForceRes -stdout -FullStdOutLogOutput' -f $project,$scriptPath
$captureProcess = Start-Process -FilePath (Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe') -ArgumentList $arguments -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $LabRoot ($Script+'.stdout.log')) -RedirectStandardError (Join-Path $LabRoot ($Script+'.stderr.log'))
$captureProcess.WaitForExit()
if ($captureProcess.ExitCode -ne 0) { throw ('Unreal exited with code '+$captureProcess.ExitCode) }
Write-Output ('Captures: '+(Join-Path $LabRoot 'captures'))
