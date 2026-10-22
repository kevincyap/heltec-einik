$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual C++ build tools are required for the host tests.' }

$output = Join-Path ([IO.Path]::GetTempPath()) ("eink-host-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $output | Out-Null
try {
    $mocks = Join-Path $PSScriptRoot 'mocks'
    $include = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\include'))
    foreach ($test in @('test_power', 'test_battery')) {
        $source = Join-Path $PSScriptRoot "$test.cpp"
        $command = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d `"$output`" && cl /nologo /EHsc /W4 /std:c++14 /I`"$mocks`" /I`"$include`" `"$source`" /Fe`"$output\$test.exe`""
        & $env:ComSpec /d /c $command
        if ($LASTEXITCODE -ne 0) { throw "Host test compilation failed: $LASTEXITCODE" }
        & (Join-Path $output "$test.exe")
        if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE" }
    }
} finally {
    Remove-Item -LiteralPath $output -Recurse -Force
}
