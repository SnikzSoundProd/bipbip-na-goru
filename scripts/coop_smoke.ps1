# ---------------------------------------------------------------------------
# Co-op smoke test: start a host and a client, let them perform the network
# handshake, and verify BOTH sides report a spawned buddy.
#
# Two SEPARATE processes are required: NetLayer uses a static instance pointer
# (s_inst), so hosting and joining inside one process would clobber each other.
# This mirrors real deployment (two running games).
#
# Why it greps logs instead of just counting processes: two alive processes
# prove nothing. The original CLI bug (`--join` ignored because argument
# parsing started at index 2) left both processes running happily in Solo mode
# with no network at all. Only the handshake lines prove co-op works.
#
# Exit code 0 = handshake verified on both sides, 1 = failed.
# ---------------------------------------------------------------------------
param()

$ErrorActionPreference = "Continue"
$root   = Split-Path -Parent $PSScriptRoot
$game   = Join-Path $root "build\game\bipbip.exe"
$hostLog = Join-Path $env:TEMP "bipbip_host.log"
$cliLog  = Join-Path $env:TEMP "bipbip_client.log"

function Say($m) { Write-Host "[coop-smoke] $m" }

if (-not (Test-Path $game)) {
    Say "FAIL: game exe not found at $game"
    exit 1
}

Say "cleaning previous instances..."
Get-Process bipbip -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Remove-Item $hostLog, $cliLog -ErrorAction SilentlyContinue

Say "starting HOST..."
$wd = Split-Path -Parent $game
$h = Start-Process -FilePath $game -ArgumentList "1337 --host" `
        -WorkingDirectory $wd -RedirectStandardError $hostLog `
        -WindowStyle Minimized -PassThru
Start-Sleep -Seconds 4

Say "starting CLIENT..."
$c = Start-Process -FilePath $game -ArgumentList "--join 127.0.0.1" `
        -WorkingDirectory $wd -RedirectStandardError $cliLog `
        -WindowStyle Minimized -PassThru
Start-Sleep -Seconds 12

$ok = $true

# --- host side ---
$hl = if (Test-Path $hostLog) { Get-Content $hostLog -Raw } else { "" }
if ($hl -match "\[net\] client connected") { Say "  host: peer connected" }
else { Say "  host: NO peer connection"; $ok = $false }

if ($hl -match "\[net\] buddy spawned") { Say "  host: buddy spawned" }
else { Say "  host: connected but buddy NOT spawned"; $ok = $false }

# --- client side ---
$cl = if (Test-Path $cliLog) { Get-Content $cliLog -Raw } else { "" }
if ($cl -match "\[net\] connected!") { Say "  client: connected" }
else { Say "  client: NOT connected"; $ok = $false }

if ($cl -match "\[net\] CLIENT buddy spawned") { Say "  client: buddy spawned" }
else { Say "  client: connected but buddy NOT spawned"; $ok = $false }

Say "stopping processes..."
Get-Process bipbip -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue

if ($ok) { Say "PASS: co-op handshake verified on both sides"; exit 0 }
else     { Say "FAIL: co-op handshake incomplete"; exit 1 }
