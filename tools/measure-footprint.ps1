<#
.SYNOPSIS
    Samples the resident footprint of the running WorldClockGadget.

.DESCRIPTION
    Takes -Samples readings -IntervalSeconds apart and prints each one plus
    the median: private bytes, working set, threads, handles, and average CPU
    per second of uptime. Short CPU comparisons are noise at 1 Hz rendering;
    CPU seconds over uptime is only meaningful for a gadget that has been
    running for a while.

    Procedure for a comparison: start the build, leave it alone for 5 minutes,
    then run this with the defaults (3 samples) and compare the medians.

.EXAMPLE
    .\tools\measure-footprint.ps1
.EXAMPLE
    .\tools\measure-footprint.ps1 -Samples 5 -IntervalSeconds 30 -Modules
#>
param(
    [ValidateRange(1, 1000)] [int] $Samples = 3,
    [ValidateRange(0, 86400)] [int] $IntervalSeconds = 60,
    # Also list the loaded modules (a DLL that shows up here costs memory).
    [switch] $Modules
)

$ErrorActionPreference = 'Stop'

function Get-Gadget {
    $p = @(Get-Process -Name WorldClockGadget -ErrorAction SilentlyContinue)
    if ($p.Count -eq 0) {
        Write-Host 'WorldClockGadget is not running.'
        exit 1
    }
    # Single-instance by design; take the first if a second is mid-exit.
    return $p[0]
}

function Get-Median([double[]] $values) {
    $sorted = $values | Sort-Object
    $n = $sorted.Count
    if ($n % 2) { return $sorted[($n - 1) / 2] }
    return ($sorted[$n / 2 - 1] + $sorted[$n / 2]) / 2
}

$rows = @()
for ($i = 1; $i -le $Samples; $i++) {
    $p = Get-Gadget
    $p.Refresh()
    $uptime = ((Get-Date) - $p.StartTime).TotalSeconds
    $rows += [pscustomobject]@{
        Sample          = $i
        'Private (MB)'  = [math]::Round($p.PrivateMemorySize64 / 1MB, 2)
        'WorkingSet (MB)' = [math]::Round($p.WorkingSet64 / 1MB, 2)
        Threads         = $p.Threads.Count
        Handles         = $p.HandleCount
        'CPU ms/s'      = [math]::Round($p.TotalProcessorTime.TotalMilliseconds / $uptime, 3)
        'Uptime (min)'  = [math]::Round($uptime / 60, 1)
    }
    if ($i -lt $Samples) { Start-Sleep -Seconds $IntervalSeconds }
}

$median = [pscustomobject]@{ Sample = 'median' }
foreach ($col in 'Private (MB)', 'WorkingSet (MB)', 'Threads', 'Handles', 'CPU ms/s', 'Uptime (min)') {
    $median | Add-Member -NotePropertyName $col -NotePropertyValue (Get-Median ($rows | ForEach-Object { $_.$col }))
}

$p = Get-Gadget
Write-Host "WorldClockGadget pid $($p.Id), started $($p.StartTime)"
($rows + $median) | Format-Table -AutoSize | Out-String | Write-Host

if ($Modules) {
    $mods = $p.Modules | Sort-Object ModuleName
    Write-Host "$($mods.Count) loaded modules:"
    $mods | Select-Object ModuleName,
        @{ Name = 'Size (KB)'; Expression = { [math]::Round($_.ModuleMemorySize / 1KB) } },
        FileName |
        Format-Table -AutoSize | Out-String | Write-Host
}
