#Requires -Version 5.1
<#
.SYNOPSIS
Download a private Windows x64 toolchain and build from your own WBFS or DOL.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\windows\build-windows.ps1 "D:\Games\Kirby.wbfs"
.EXAMPLE
.\tools\windows\build-windows.ps1 "D:\Games\main.dol" -Jobs 4 -TranslatedJobs 2
#>
[CmdletBinding()]
param(
    [Parameter(Position=0)][string]$Source,
    [ValidateRange(1,256)][int]$Jobs = 2,
    [ValidateRange(1,256)][int]$TranslatedJobs = 2,
    [switch]$ToolsOnly,
    [switch]$PackageOnly,
    [string]$CacheRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path (Split-Path $PSScriptRoot)
$Cache = if ($CacheRoot) { [IO.Path]::GetFullPath($CacheRoot) } else { Join-Path $Root '.tools\windows' }
$Manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows-dependencies.json') -Raw | ConvertFrom-Json

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Program exited with code $LASTEXITCODE" }
}

function Get-Package([string]$Name) {
    $Spec = $Manifest.$Name
    $Id = "$Name-$($Spec.hash.Substring(0,16))"
    $Archive = Join-Path $Cache "downloads\$Id.$($Spec.format)"
    if (!(Test-Path -LiteralPath $Archive) -or
        (Get-FileHash -LiteralPath $Archive -Algorithm $Spec.algorithm).Hash -ne $Spec.hash) {
        Write-Host "Downloading $Name ..."
        $Partial = "$Archive.partial"
        for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
            try {
                Invoke-WebRequest -UseBasicParsing -Uri $Spec.url -OutFile $Partial
                if ((Get-FileHash -LiteralPath $Partial -Algorithm $Spec.algorithm).Hash -ne $Spec.hash) {
                    throw "Checksum mismatch for $Name"
                }
                Move-Item -LiteralPath $Partial -Destination $Archive -Force
                break
            } catch {
                if ($Attempt -eq 3) { throw }
                Write-Warning "Download failed; retrying $Name ($Attempt/3)"
            }
        }
    }
    $Destination = Join-Path $Cache $Id
    $Entry = Join-Path $Destination $Spec.entry
    if (!(Test-Path -LiteralPath (Join-Path $Destination '.complete'))) {
        # Publish only complete packages. An interrupted extraction is never reused.
        if (Test-Path -LiteralPath $Destination) { throw "Incomplete tool directory: $Destination" }
        do {
            $Staging = Join-Path $Cache ('.s-' + [guid]::NewGuid().ToString('N').Substring(0,8))
        } while (Test-Path -LiteralPath $Staging)
        New-Item -ItemType Directory -Path $Staging | Out-Null
        Write-Host "Extracting $Name ..."
        if ($Spec.format -eq 'zip') {
            Invoke-Checked (Join-Path $env:SystemRoot 'System32\tar.exe') @('-xf', $Archive, '-C', $Staging)
        } elseif ($Spec.format -eq '7z') {
            $SevenZip = Get-Package 'sevenzip'
            Invoke-Checked $SevenZip @('x', '-y', "-o$Staging", $Archive)
        } else {
            Copy-Item -LiteralPath $Archive -Destination (Join-Path $Staging $Spec.entry)
        }
        if (!(Test-Path -LiteralPath (Join-Path $Staging $Spec.entry))) {
            throw "Downloaded $Name lacks $($Spec.entry)"
        }
        Set-Content -LiteralPath (Join-Path $Staging '.complete') -Value $Spec.hash
        Move-Item -LiteralPath $Staging -Destination $Destination
    }
    if (!(Test-Path -LiteralPath $Entry)) { throw "Damaged tool cache: $Entry" }
    return $Entry
}

$SavedEnvironment = @{}
foreach ($Name in @('PATH', 'DOTNET_ROOT', 'DOTNET_MULTILEVEL_LOOKUP', 'DOTNET_CLI_TELEMETRY_OPTOUT',
                    'DOTNET_NOLOGO', 'PYTHONUTF8', 'KIRBY_PROJECT_ROOT')) {
    $SavedEnvironment[$Name] = [Environment]::GetEnvironmentVariable($Name, 'Process')
}
$Lock = $null
$ExitCode = 0
try {
    if ($env:OS -ne 'Windows_NT' -or ![Environment]::Is64BitProcess -or
        $env:PROCESSOR_ARCHITECTURE -ne 'AMD64') {
        throw 'Run with 64-bit PowerShell on Windows x64.'
    }
    if ($ToolsOnly -and $PackageOnly) { throw 'Use either -ToolsOnly or -PackageOnly.' }
    if (!$ToolsOnly -and !$PackageOnly -and !$Source) { throw 'Supply a WBFS or main.dol path (or use -PackageOnly / -ToolsOnly).' }
    if ($Source) {
        $Source = (Resolve-Path -LiteralPath $Source).Path
        if (!(Test-Path -LiteralPath $Source -PathType Leaf) -or
            [IO.Path]::GetExtension($Source).ToLowerInvariant() -notin @('.dol', '.wbfs')) {
            throw 'Input must be a WBFS or DOL file.'
        }
        if ([IO.Path]::GetExtension($Source) -ieq '.dol' -and
            (Get-FileHash -LiteralPath $Source -Algorithm SHA1).Hash -ne 'f4280a2b0709bfde4132be22e9f7fa8ecfa2a130') {
            throw 'Unsupported DOL: only RK5E01 Rev 0 (USA) is supported.'
        }
    }
    if (!(Test-Path -LiteralPath (Join-Path $Root '.git'))) { throw 'Run from a Git checkout, not a source ZIP.' }
    New-Item -ItemType Directory -Force -Path (Join-Path $Cache 'downloads') | Out-Null
    $Lock = [IO.File]::Open((Join-Path $Cache 'build.lock'), 'OpenOrCreate', 'ReadWrite', 'None')
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $Python = Get-Package 'python'
    # Select modules per process, so concurrent snapshots can share this Python.
    # Remove old checkout-specific paths from earlier private tool installations.
    $env:KIRBY_PROJECT_ROOT = $Root
    $PythonHome = Split-Path $Python
    $Pth = Get-ChildItem -LiteralPath $PythonHome -Filter 'python*._pth' | Select-Object -First 1
    $PthLines = @(Get-Content -LiteralPath $Pth.FullName | Where-Object {
        ![IO.Path]::IsPathRooted($_) -and $_.Trim() -notmatch '^#?\s*import site$'
    })
    $SiteSource = Join-Path $PSScriptRoot 'sitecustomize.py'
    $SiteTarget = Join-Path $PythonHome 'sitecustomize.py'
    if (!(Test-Path -LiteralPath $SiteTarget) -or
        (Get-FileHash -LiteralPath $SiteSource -Algorithm SHA256).Hash -ne (Get-FileHash -LiteralPath $SiteTarget -Algorithm SHA256).Hash) {
        Copy-Item -LiteralPath $SiteSource -Destination $SiteTarget -Force
    }
    $ConfiguredPth = [string[]]($PthLines + 'import site')
    if (($ConfiguredPth -join "`n") -ne ((Get-Content -LiteralPath $Pth.FullName) -join "`n")) {
        [IO.File]::WriteAllLines($Pth.FullName, $ConfiguredPth, [Text.UTF8Encoding]::new($false))
    }
    $Redist = Get-Package 'vcredist'
    if ($PackageOnly) {
        Invoke-Checked $Python @((Join-Path $PSScriptRoot 'package_windows.py'), '--redist', $Redist)
        exit 0
    }
    # Archive imports need 7zr even if every ZIP package was already cached.
    $SevenZip = Get-Package 'sevenzip'
    $Git = Get-Package 'git'
    $Cmake = Get-Package 'cmake'
    $Ninja = Get-Package 'ninja'
    $Dotnet = Get-Package 'dotnet'
    $Clang = Get-Package 'mingw'
    $WinrtBase = Get-Package 'sdk'
    # clang lives in <package>/<llvm-mingw-version>/bin.
    $MingwRoot = Split-Path (Split-Path $Clang)
    $WinrtInclude = Split-Path (Split-Path $WinrtBase)
    $Bins = @($Python, $Git, $Cmake, $Ninja, $Dotnet, $Clang, $SevenZip) | ForEach-Object { Split-Path $_ }
    $env:PATH = ($Bins -join ';') + ';' + $env:PATH
    $env:DOTNET_ROOT = Split-Path $Dotnet
    $env:DOTNET_MULTILEVEL_LOOKUP = '0'
    $env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
    $env:DOTNET_NOLOGO = '1'
    $env:PYTHONUTF8 = '1'

    # Dawn and Dolphin require Microsoft's native runtime. All build tools above
    # are private; this is the only system installation and can request UAC.
    $MissingRuntime = @(@('vcruntime140.dll','vcruntime140_1.dll','msvcp140.dll') | Where-Object {
        !(Test-Path -LiteralPath (Join-Path "$env:SystemRoot\System32" $_))
    })
    if ($MissingRuntime.Count -gt 0) {
        if ((Get-AuthenticodeSignature -LiteralPath $Redist).Status -ne 'Valid') {
            throw 'Microsoft VC++ runtime installer signature is invalid.'
        }
        Write-Host 'Installing Microsoft VC++ x64 runtime (Windows may request elevation) ...'
        $Process = Start-Process -FilePath $Redist -ArgumentList '/install','/quiet','/norestart' -WindowStyle Hidden -Wait -PassThru
        if ($Process.ExitCode -notin @(0,1638,3010)) { throw "VC++ runtime installation failed: $($Process.ExitCode)" }
        if ($Process.ExitCode -eq 3010) { throw 'VC++ runtime installed. Restart Windows, then rerun this command.' }
    }
    if ($ToolsOnly -or [IO.Path]::GetExtension($Source) -ieq '.wbfs') {
        $Dolphin = Get-Package 'dolphin'
        $env:PATH = (Split-Path $Dolphin) + ';' + $env:PATH
    }
    foreach ($Command in @(@($Python,'--version'), @($Git,'--version'), @($Cmake,'--version'),
                            @($Ninja,'--version'), @($Dotnet,'--version'), @($Clang,'--version'))) {
        Invoke-Checked $Command[0] @($Command[1])
    }
    if (!$ToolsOnly) {
        Invoke-Checked $Python @((Join-Path $PSScriptRoot 'windows_build.py'), $Source,
            '--jobs', "$Jobs", '--translated-jobs', "$TranslatedJobs",
            '--llvm-mingw-root', $MingwRoot, '--cppwinrt-include', $WinrtInclude, '--redist', $Redist)
    } else {
        Write-Host "All Windows build tools are ready in $Cache"
    }
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    $ExitCode = 1
} finally {
    if ($null -ne $Lock) { $Lock.Dispose() }
    foreach ($Name in $SavedEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($Name, $SavedEnvironment[$Name], 'Process')
    }
}
exit $ExitCode
