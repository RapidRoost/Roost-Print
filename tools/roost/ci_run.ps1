# Roost Print CI: run a build command, keep its whole output in a log, and fail loudly.
#
# The build scripts (test_build.bat) carry on after errors and still exit 0, so success is judged
# by whether the thing that should exist (-Check) does. On failure, the first error lines and the
# end of the log are posted as error notes on the run, where they can be read without the log.
param(
    [Parameter(Mandatory)] [string] $Log,
    [Parameter(Mandatory)] [string] $Command,
    [Parameter(Mandatory)] [string] $Check
)
$ErrorActionPreference = 'Continue'

cmd /c "$Command 2>&1" | Tee-Object -FilePath $Log | Out-Host
$code = $LASTEXITCODE

if ($code -eq 0 -and (Test-Path $Check)) {
    Write-Host "OK: $Check"
    exit 0
}

function Note([string] $title, [string[]] $lines) {
    $text = ($lines -join "`n")
    if ($text.Length -gt 3800) { $text = $text.Substring($text.Length - 3800) }
    $text = $text.Replace('%', '%25').Replace("`r", '').Replace("`n", '%0A')
    Write-Host "::error title=$title::$text"
}

$all = Get-Content $Log
$errors = $all | Select-String -Pattern '(error [A-Z]+\d+|: error|fatal error|CMake Error|Error:|FAILED|cannot open|not found)' -CaseSensitive:$false |
    Select-Object -First 40 | ForEach-Object { $_.Line.Trim() }
if ($errors) { Note "First errors ($Log)" $errors }
Note "End of $Log" ($all | Select-Object -Last 45)
Write-Host "Failed: exit $code, $Check exists: $(Test-Path $Check)"
exit 1
