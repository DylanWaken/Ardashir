param(
    [string]$ProjectRoot = 'D:/UnrealProjects/ArdashirUnrealTest',
    [string]$EngineRoot = 'D:/UnrealEngine',
    [int]$TimeoutSeconds = 900,
    [switch]$Contracts
)
$ErrorActionPreference = 'Stop'
$ProjectRoot = [IO.Path]::GetFullPath($ProjectRoot)
$ProjectFile = Join-Path $ProjectRoot 'ArdashirUnrealTest.uproject'
$Editor = Join-Path $EngineRoot 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$ResultPath = Join-Path $ProjectRoot 'Saved/ArdaHandoffResult.json'
$LogPath = Join-Path $ProjectRoot 'Saved/Logs/ArdaHandoff.log'
if (-not (Test-Path -LiteralPath $ProjectFile)) { throw 'Run ArdaSetupUnrealTest.ps1 -Build first.' }
New-Item -ItemType Directory -Path (Split-Path $LogPath -Parent) -Force | Out-Null
$Arguments = @(
    "`"$ProjectFile`"", '/Engine/Maps/Templates/Template_Default', '-game', '-d3d12',
    '-ArdaValidate', '-unattended', '-nosplash', '-nosound', '-RenderOffscreen',
    '-ResX=640', '-ResY=360', '-windowed', "-abslog=`"$LogPath`""
)
if ($Contracts)
{
    $LogPath = Join-Path $ProjectRoot 'Saved/Logs/ArdaProviderContracts.log'
    # Each run owns a new report directory; prior reports cannot satisfy this run.
    $ReportPath = Join-Path $ProjectRoot ('Saved/Automation/ArdaProviderContracts/' + [Guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $LogPath) { Remove-Item -LiteralPath $LogPath }
    $Arguments = @(
        "`"$ProjectFile`"", '-d3d12', '-ArdaDisablePostProcess', '-unattended', '-nosplash',
        '-nosound', '-RenderOffscreen', '-ExecCmds="Automation RunTests Ardashir.Unreal.ProviderContract"',
        '-TestExit="Automation Test Queue Empty"', "-ReportExportPath=`"$ReportPath`"", "-abslog=`"$LogPath`""
    )
}
else
{
    # Preserve the last positive handoff result when running only provider contracts.
    if (Test-Path -LiteralPath $ResultPath) { Remove-Item -LiteralPath $ResultPath }
}
$Process = Start-Process -FilePath $Editor -ArgumentList $Arguments -WindowStyle Hidden -PassThru
if (-not $Process.WaitForExit($TimeoutSeconds * 1000))
{
    Stop-Process -Id $Process.Id -Force
    throw "Unreal handoff test timed out. See $LogPath"
}
if ($Contracts)
{
    if ($Process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $LogPath))
    {
        throw "Provider contract test failed (exit $($Process.ExitCode)). See $LogPath"
    }
    $Log = Get-Content -LiteralPath $LogPath -Raw
    if ($Log -notmatch 'Test Completed\. Result=\{Success\} Name=\{ProviderContract\} Path=\{Ardashir\.Unreal\.ProviderContract\}')
    {
        throw "The provider contract test did not report success. See $LogPath"
    }
    Write-Output "PASS: Ardashir.Unreal.ProviderContract. $LogPath"
    return
}
if ($Process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $ResultPath))
{
    throw "Unreal handoff test failed (exit $($Process.ExitCode)); no passing result. See $LogPath"
}
$Result = Get-Content -LiteralPath $ResultPath -Raw | ConvertFrom-Json
if (-not $Result.passed -or $Result.completedFrames -lt 6 -or $Result.graphSubmissions -lt 6 -or $Result.registeredNodes -ne 2)
{
    throw "Unreal handoff assertions failed. See $ResultPath and $LogPath"
}
Write-Output "PASS: $($Result.completedFrames) Unreal -> Arda -> CUDA -> Unreal frames, 64 RGBA assertions/frame. $ResultPath"
