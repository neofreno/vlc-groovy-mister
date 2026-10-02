param([ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [switch]$Test)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'Se requiere Visual Studio 2022 con C++ v143 y Windows SDK.' }
& $msbuild (Join-Path $root 'vlc-groovy-mister.sln') /m /t:Build "/p:Configuration=$Configuration" /p:Platform=x64 /v:minimal /nologo
if ($LASTEXITCODE) { throw 'Error compilando el plugin/API.' }
if ($Test) {
    $build = Join-Path $root 'build/tests'
    & cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE) { throw 'Error configurando pruebas.' }
    & cmake --build $build --config $Configuration --parallel
    if ($LASTEXITCODE) { throw 'Error compilando pruebas.' }
    & ctest --test-dir $build -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw 'Pruebas fallidas.' }
}
