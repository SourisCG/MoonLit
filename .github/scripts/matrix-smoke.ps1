# Automated smoke rows of the manual matrix (S3, S4, S7): launches the app,
# verifies the MoonLit window appears and stays alive, then exits cleanly.
# Usage: pwsh -NoProfile -File matrix-smoke.ps1 [-Exe <path to MoonLit.exe>]

param(
    [string]$Exe = (Join-Path (Get-Location) "build_moonlit_v1_x64\rundir\RelWithDebInfo\bin\64bit\MoonLit.exe")
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "MoonLit.exe not found: $Exe"
}

Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

$ciBefore = (Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-CodeIntegrity/Operational'; Id=3076,3077} -ErrorAction SilentlyContinue | Measure-Object).Count

$process = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru
Start-Sleep -Seconds 6

if (-not $process.HasExited) {
    # After a force-kill from a previous run the app may offer the
    # "unclean shutdown" dialog; accept normal mode so the main window shows.
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $windows = $root.FindAll([System.Windows.Automation.TreeScope]::Children,
        [System.Windows.Automation.Condition]::TrueCondition) |
        Where-Object { $_.Current.ProcessId -eq $process.Id }
    foreach ($window in $windows) {
        if ($window.Current.Name -like '*Crash Detected*') {
            $buttons = $window.FindAll([System.Windows.Automation.TreeScope]::Descendants,
                [System.Windows.Automation.Condition]::TrueCondition) |
                Where-Object { $_.Current.Name -eq 'Run in Normal Mode' }
            foreach ($button in $buttons) {
                $pattern = $button.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern)
                $pattern.Invoke()
                break
            }
            Start-Sleep -Seconds 6
            break
        }
    }
}

Start-Sleep -Seconds 2

if ($process.HasExited) {
    Write-Host "FAIL S3: MoonLit exited early with code $($process.ExitCode)"
    exit 1
}

$root = [System.Windows.Automation.AutomationElement]::RootElement
$windows = $root.FindAll([System.Windows.Automation.TreeScope]::Children,
    [System.Windows.Automation.Condition]::TrueCondition) |
    Where-Object { $_.Current.ProcessId -eq $process.Id }
$mainWindow = $windows | Where-Object { $_.Current.Name -eq 'MoonLit' -and $_.Current.ClassName -eq 'OBSBasic' } |
    Select-Object -First 1

if (-not $mainWindow) {
    Write-Host "FAIL S4: no MoonLit main window (found: $($windows | ForEach-Object { $_.Current.Name }))"
    Stop-Process -Id $process.Id -Force
    exit 1
}
Write-Host "PASS S3: MoonLit stays running (pid $($process.Id))"
Write-Host "PASS S4: MoonLit window visible"

$crashesBefore = (Get-ChildItem "$env:APPDATA\MoonLit\obs-studio\crashes" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-3) } | Measure-Object).Count
Stop-Process -Id $process.Id -Force
Start-Sleep -Seconds 2

$ciAfter = (Get-WinEvent -FilterHashtable @{LogName='Microsoft-Windows-CodeIntegrity/Operational'; Id=3076,3077} -ErrorAction SilentlyContinue | Measure-Object).Count
$crashesAfter = (Get-ChildItem "$env:APPDATA\MoonLit\obs-studio\crashes" -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-3) } | Measure-Object).Count

if ($ciAfter -gt $ciBefore) {
    Write-Host "FAIL S7: new CodeIntegrity blocks detected"
    exit 1
}
if ($crashesAfter -gt $crashesBefore) {
    Write-Host "FAIL S7: new crash reports detected"
    exit 1
}
Write-Host "PASS S7: no CodeIntegrity blocks and no crashes"
Write-Host "smoke matrix passed"
