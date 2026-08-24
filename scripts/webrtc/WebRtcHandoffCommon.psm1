Set-StrictMode -Version Latest

function Clear-WebRtcHandoffFiles {
    param(
        [Parameter(Mandatory = $true)][string[]]$Roots,
        [Parameter(Mandatory = $true)][string]$ManagedRoot
    )
    foreach ($root in $Roots) {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        foreach ($file in @(Get-ChildItem -LiteralPath $root -Filter '*.json' `
                -File -ErrorAction SilentlyContinue)) {
            $resolved = [IO.Path]::GetFullPath($file.FullName)
            $boundary = [IO.Path]::GetFullPath($ManagedRoot).TrimEnd('\') + '\'
            if (-not $resolved.StartsWith(
                    $boundary,[StringComparison]::OrdinalIgnoreCase)) {
                throw "Handoff file escaped its managed root: $resolved"
            }
            Remove-Item -LiteralPath $resolved -Force
        }
    }
}

function Sync-WebRtcHandoffFiles {
    param(
        [Parameter(Mandatory = $true)][string]$ExchangeRoot,
        [Parameter(Mandatory = $true)][string]$InboxRoot,
        [Parameter(Mandatory = $true)][string]$OutboxRoot
    )
    foreach ($root in @($ExchangeRoot,$InboxRoot,$OutboxRoot)) {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $InboxRoot -Filter '*.json' `
            -File -ErrorAction SilentlyContinue)) {
        $destination = Join-Path $ExchangeRoot $file.Name
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
    }
    foreach ($file in @(Get-ChildItem -LiteralPath $ExchangeRoot -Filter '*.json' `
            -File -ErrorAction SilentlyContinue)) {
        $destination = Join-Path $OutboxRoot $file.Name
        if (-not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Copy-Item -LiteralPath $file.FullName -Destination $destination
        }
    }
}

function Read-WebRtcJsonEvent {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Name
    )
    foreach ($line in @(Get-Content -LiteralPath $Path `
            -ErrorAction SilentlyContinue)) {
        try {
            $value = $line | ConvertFrom-Json -ErrorAction Stop
            if ([string]$value.event -eq $Name) { return $value }
        } catch { }
    }
    return $null
}

Export-ModuleMember -Function @(
    'Clear-WebRtcHandoffFiles',
    'Sync-WebRtcHandoffFiles',
    'Read-WebRtcJsonEvent'
)
