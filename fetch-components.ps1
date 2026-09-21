[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$projectRoot = $PSScriptRoot
$sourceFile = Join-Path $projectRoot "component-sources.json"
$dependencyRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot ".deps"))
$checkoutPath = [IO.Path]::GetFullPath((Join-Path $dependencyRoot "esp-brookesia"))
$projectPrefix = [IO.Path]::GetFullPath($projectRoot).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar

if (-not $checkoutPath.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to place dependencies outside the project: $checkoutPath"
}
if ($null -eq (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH."
}
if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
    throw "Component source manifest is missing: $sourceFile"
}

$source = (Get-Content -Raw -LiteralPath $sourceFile | ConvertFrom-Json).espBrookesia
$repository = [string]$source.repository
$commit = [string]$source.commit
$sparsePaths = @($source.paths | ForEach-Object { [string]$_ })
if ([string]::IsNullOrWhiteSpace($repository) -or $commit -notmatch '^[0-9a-fA-F]{40}$' -or $sparsePaths.Count -eq 0) {
    throw "Invalid espBrookesia entry in $sourceFile"
}

function Invoke-Git([string[]]$Arguments) {
    & git @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE."
    }
}

New-Item -ItemType Directory -Force -Path $dependencyRoot | Out-Null
$gitMarker = Join-Path $checkoutPath ".git"
if (-not (Test-Path -LiteralPath $gitMarker)) {
    if (Test-Path -LiteralPath $checkoutPath) {
        $existingEntries = @(Get-ChildItem -Force -LiteralPath $checkoutPath)
        if ($existingEntries.Count -ne 0) {
            throw "Dependency path exists but is not a Git checkout: $checkoutPath"
        }
        Remove-Item -LiteralPath $checkoutPath
    }
    Invoke-Git @("clone", "--filter=blob:none", "--no-checkout", $repository, $checkoutPath)
}
else {
    $changes = @(& git -C $checkoutPath status --porcelain --untracked-files=normal)
    if ($LASTEXITCODE -ne 0) {
        throw "Unable to inspect the existing dependency checkout at $checkoutPath"
    }
    if ($changes.Count -gt 0) {
        throw "The dependency checkout contains local changes. Clean $checkoutPath before updating it."
    }
    Invoke-Git @("-C", $checkoutPath, "remote", "set-url", "origin", $repository)
}

Invoke-Git @("-C", $checkoutPath, "sparse-checkout", "init", "--cone")
Invoke-Git (@("-C", $checkoutPath, "sparse-checkout", "set") + $sparsePaths)
$currentCommit = (& git -C $checkoutPath rev-parse HEAD 2>$null).Trim()
$hasSparseFiles = $true
foreach ($relativePath in $sparsePaths) {
    if (-not (Test-Path -LiteralPath (Join-Path $checkoutPath $relativePath) -PathType Container)) {
        $hasSparseFiles = $false
        break
    }
}
if ($LASTEXITCODE -ne 0 -or $currentCommit -ne $commit) {
    Invoke-Git @("-C", $checkoutPath, "fetch", "--depth", "1", "origin", $commit)
    Invoke-Git @("-C", $checkoutPath, "checkout", "--detach", "FETCH_HEAD")
}
elseif (-not $hasSparseFiles) {
    # A clone made with --no-checkout has a valid HEAD but an empty index and
    # working tree. Materialize the selected paths without another fetch.
    Invoke-Git @("-C", $checkoutPath, "checkout", "--detach", "--force", $commit)
}

$resolvedCommit = (& git -C $checkoutPath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $resolvedCommit -ne $commit) {
    throw "Expected ESP-Brookesia commit $commit but checked out $resolvedCommit."
}
foreach ($relativePath in $sparsePaths) {
    $requiredPath = Join-Path $checkoutPath $relativePath
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Container)) {
        throw "Sparse checkout did not provide required component: $relativePath"
    }
}

Write-Host "ESP-Brookesia components are ready at commit $resolvedCommit."
