param([switch]$IncludeCrowPanel)

$ErrorActionPreference = 'Stop'
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual C++ build tools are required for the host tests.' }

$output = Join-Path ([IO.Path]::GetTempPath()) ("eink-host-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $output | Out-Null
try {
    $mocks = Join-Path $PSScriptRoot 'mocks'
    $include = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\include'))
    $tests = @('test_power', 'test_battery', 'test_buttons', 'test_menu', 'test_state', 'test_book_storage', 'test_reader', 'test_diagnostics', 'test_serial_output')
    if ($IncludeCrowPanel) { $tests += 'test_crowpanel_buttons' }
    foreach ($test in $tests) {
        $sourceName = if ($test -eq 'test_crowpanel_buttons') { 'test_buttons' } else { $test }
        $defines = if ($test -eq 'test_crowpanel_buttons') { '/DBOARD_CROWPANEL_E213' } else { '' }
        $source = Join-Path $PSScriptRoot "$sourceName.cpp"
        $serialSource = Join-Path $PSScriptRoot '..\..\src\serial_output.cpp'
        $command = "set `"PATH=${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer;%PATH%`" && call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && cd /d `"$output`" && cl /nologo /EHsc /W4 /std:c++14 /DARDUINO_USB_CDC_ON_BOOT=1 $defines /I`"$mocks`" /I`"$include`" `"$source`" `"$serialSource`" /Fe`"$output\$test.exe`""
        & $env:ComSpec /d /c $command
        if ($LASTEXITCODE -ne 0) { throw "Host test compilation failed: $LASTEXITCODE" }
        & (Join-Path $output "$test.exe")
        if ($LASTEXITCODE -ne 0) { throw "Host tests failed: $LASTEXITCODE" }
    }
} finally {
    Remove-Item -LiteralPath $output -Recurse -Force
}
