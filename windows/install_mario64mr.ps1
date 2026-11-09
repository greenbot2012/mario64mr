<#
Mario 64 MR on Windows, without WSL (install_mario64mr.bat runs this).

Checks your ROM, sets up its own build tools the first time, all in one folder (no admin, nothing installed
system-wide), then runs install_mario64mr.py with them to make the game and send it to the headset:

  MSYS2 (make, git, patch, Python, gcc)   %LOCALAPPDATA%\Mario64MR\msys64
  adb (Android platform tools)            %LOCALAPPDATA%\Mario64MR\platform-tools (unless you have adb already)
  the Android NDK, sources and builds     %LOCALAPPDATA%\Mario64MR\work
  the game made from your ROM             Mario64MRData\ (next to this repository's README)

(C:\Mario64MR instead, if your user folder's name has spaces or other characters the build tools can't take.)
To start over, delete that folder.
#>
param(
    [string]$Rom,          # your ROM (else a file dialog asks for it)
    [switch]$NoPush,       # make the game but don't send it to the headset
    [switch]$Check         # only check the ROM
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"  # (downloads are many times slower with PowerShell's progress bar)
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$Repo = Split-Path -Parent $PSScriptRoot
$Root = Join-Path $env:LOCALAPPDATA "Mario64MR"
if ($Root -notmatch '^[A-Za-z0-9_:\\.-]+$') { $Root = Join-Path $env:SystemDrive "Mario64MR" }
$Msys = Join-Path $Root "msys64"
$Bash = Join-Path $Msys "usr\bin\bash.exe"

# The unmodified US ROM's SHA-1 in each dump's byte order (install_mario64mr.py checks it again, in detail)
$UsSha1 = @{
    "9BEF1128717F958171A4AFAC3ED78EE2BB4E86CE" = ".z64";
    "1002DD7B56AA0A59A9103F1FB3D57D6B161F8DA7" = ".v64";
    "39A4B853CA4DEEE3FE69EB20C0FFD0847C5B4367" = ".n64"
}

function Fail($message) {
    Write-Host ""
    Write-Host $message -ForegroundColor Red
    exit 1
}

function Download($url, $file) {
    Write-Host "Downloading $url ..."
    Invoke-WebRequest -Uri $url -OutFile "$file.part" -UseBasicParsing
    Move-Item -Force "$file.part" $file
}

# What a file that isn't the US ROM is, from its first bytes (the same reasons as install_mario64mr.py gives)
function Describe-Rom($path) {
    $stream = [IO.File]::OpenRead($path)
    try {
        $head = New-Object byte[] 64
        $read = $stream.Read($head, 0, 64)
        $size = $stream.Length
    } finally { $stream.Close() }
    $magic = [BitConverter]::ToString($head, 0, 4)
    if ($magic.StartsWith("50-4B-03-04")) { return "this is a .zip archive, not a ROM: extract it first, and choose the .z64 / .v64 / .n64 file inside" }
    if ($magic.StartsWith("37-7A-BC-AF")) { return "this is a .7z archive, not a ROM: extract it first, and choose the .z64 / .v64 / .n64 file inside" }
    if ($magic.StartsWith("52-61-72-21")) { return "this is a .rar archive, not a ROM: extract it first, and choose the .z64 / .v64 / .n64 file inside" }
    $z64 = New-Object byte[] 64
    switch ($magic) {
        "80-37-12-40" { [Array]::Copy($head, $z64, 64) }
        "37-80-40-12" { for ($i = 0; $i -lt 64; $i += 2) { $z64[$i] = $head[$i + 1]; $z64[$i + 1] = $head[$i] } }
        "40-12-37-80" { for ($i = 0; $i -lt 64; $i++) { $z64[$i] = $head[($i -band -4) + 3 - ($i % 4)] } }
        default { return "not a Nintendo 64 ROM (its header isn't one): use a Super Mario 64 (USA) dump, a .z64, .v64 or .n64 file of about 8 MB" }
    }
    $name = [Text.Encoding]::ASCII.GetString($z64, 0x20, 20).Trim([char]0, ' ')
    $code = [Text.Encoding]::ASCII.GetString($z64, 0x3B, 4)
    if ($code.Substring(1, 2) -ne "SM" -or $name.ToUpper() -notmatch "MARIO") { return "this is an N64 ROM of `"$name`" (game code $code), not Super Mario 64" }
    $region = @{ "E" = "USA"; "J" = "Japan"; "P" = "Europe" }[$code.Substring(3)]
    if ($code -eq "NSMJ" -and $z64[0x3F] -eq 3) { $region = "Japan, Shindou edition" }
    if ($code -ne "NSME") { return "this is Super Mario 64 ($region): only the USA release is supported" }
    if ($size -ne 8MB) { return "Super Mario 64 (USA) by its header, but $([math]::Round($size / 1MB)) MB instead of 8 MB: a ROM hack (they're often extended) or a bad dump" }
    return "Super Mario 64 (USA) by its header, but its contents differ: a ROM hack, a patched or trainer ROM, or a bad dump"
}

Write-Host "Mario 64 MR: make the game from your ROM and put it on your headset" -ForegroundColor Cyan

# 1. The ROM, checked before anything is downloaded
if (-not $Rom) {
    Add-Type -AssemblyName System.Windows.Forms
    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = "Choose your Super Mario 64 (USA) ROM"
    $dialog.Filter = "Nintendo 64 ROM (*.z64;*.v64;*.n64)|*.z64;*.v64;*.n64|All files (*.*)|*.*"
    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail "No ROM chosen: nothing was done." }
    $Rom = $dialog.FileName
}
if (-not (Test-Path -LiteralPath $Rom -PathType Leaf)) { Fail "${Rom}: no such file." }
$Rom = (Resolve-Path -LiteralPath $Rom).Path
$sha1 = (Get-FileHash -LiteralPath $Rom -Algorithm SHA1).Hash
if (-not $UsSha1.ContainsKey($sha1)) {
    Fail ("${Rom}:`n  " + (Describe-Rom $Rom) + "`n  An unmodified Super Mario 64 (USA) dump has SHA-1 9bef1128717f958171a4afac3ed78ee2bb4e86ce (as .z64).`n  Nothing was downloaded, built or sent to the headset.")
}
Write-Host "ROM: Super Mario 64 (USA), $($UsSha1[$sha1]), SHA-1 matches" -ForegroundColor Green
if ($Check) { exit 0 }

# 2. MSYS2, its own copy, the first time (a few minutes)
New-Item -ItemType Directory -Force $Root | Out-Null
if (-not (Test-Path $Bash)) {
    Write-Host "Setting up the build tools in $Root (the first time only: about 10 minutes)..." -ForegroundColor Cyan
    $sfx = Join-Path $Root "msys2-base.sfx.exe"
    Download "https://github.com/msys2/msys2-installer/releases/download/nightly-x86_64/msys2-base-x86_64-latest.sfx.exe" $sfx
    & $sfx -y "-o$Root" | Out-Null
    if (-not (Test-Path $Bash)) { Fail "MSYS2 didn't unpack into $Msys. Delete $Root and try again." }
    Remove-Item $sfx
}
$env:MSYSTEM = "MSYS"
$env:CHERE_INVOKING = "1"       # (the shell starts where we are, not in its home)
$env:MSYS2_PATH_TYPE = "minimal" # (only MSYS2's own tools, not whatever else is on your PATH)
Set-Location $Repo
$ready = Join-Path $Root "msys2-ready"
if (-not (Test-Path $ready)) {
    Write-Host "Installing make, git, patch, Python and gcc into MSYS2..." -ForegroundColor Cyan
    # (a fresh MSYS2 updates its own core first, which ends that run: so twice)
    & $Bash -lc "pacman -Syuu --noconfirm --disable-download-timeout"
    & $Bash -lc "pacman -Syuu --noconfirm --disable-download-timeout"
    & $Bash -lc "pacman -S --needed --noconfirm --disable-download-timeout make git patch python gcc"
    if ($LASTEXITCODE -ne 0) { Fail "Installing the build tools failed (see above). Check your internet connection and try again." }
    Set-Content $ready "ok"
}

# 3. adb: yours if you have it (two different ones fight over the headset), else its own
$adbDir = $null
if (-not $NoPush) {
    $existing = Get-Command adb.exe -ErrorAction SilentlyContinue
    if ($existing) {
        $adbDir = Split-Path -Parent $existing.Source
    } else {
        $adbDir = Join-Path $Root "platform-tools"
        if (-not (Test-Path (Join-Path $adbDir "adb.exe"))) {
            $zip = Join-Path $Root "platform-tools.zip"
            Download "https://dl.google.com/android/repository/platform-tools-latest-windows.zip" $zip
            Expand-Archive -LiteralPath $zip -DestinationPath $Root -Force
            Remove-Item $zip
        }
    }
}

# The app, to install with the game: Mario64MR.apk here (next to the README) or in Downloads, the newest
$apk = $null
if (-not $NoPush) {
    $folders = @($Repo, (Join-Path $env:USERPROFILE "Downloads"))
    $apk = Get-ChildItem -LiteralPath $folders -Filter "Mario64MR*.apk" -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($apk) { Write-Host "The app: $($apk.FullName) (installed on the headset with the game)" }
    else { Write-Host "No Mario64MR.apk here or in Downloads: if the app isn't on the headset yet, download it from the Releases page first." -ForegroundColor Yellow }
}

# 4. Make the game, and send it to the headset
$env:M64_REPO = $Repo
$env:M64_APK = if ($apk) { $apk.FullName } else { "" }
$env:M64_ROM = $Rom
$env:M64_WORK = Join-Path $Root "work"
$env:M64_ADB = if ($adbDir) { $adbDir } else { "" }
$env:M64_PUSH = if ($NoPush) { "" } else { "1" }
# (a script, the paths in the environment: Windows PowerShell drops quotes inside the arguments it passes)
& $Bash -l (Join-Path $PSScriptRoot "make_game.sh").Replace('\', '/')
if ($LASTEXITCODE -ne 0) { Fail "Making the game failed (see above). Running install_mario64mr.bat again picks up where it stopped." }
Write-Host ""
Write-Host "Done." -ForegroundColor Green
