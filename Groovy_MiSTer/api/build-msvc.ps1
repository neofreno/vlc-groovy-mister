param([ValidateSet('Debug','Release')][string]$Configuration = 'Release',
      [ValidateSet('x64','Win32')][string]$Platform = 'x64')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild with the Visual C++ v143 tools is required.' }
& $msbuild (Join-Path $PSScriptRoot 'groovymister.vcxproj') /m /t:Build "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal /nologo
if ($LASTEXITCODE) { throw "API build failed: $LASTEXITCODE" }
Write-Output "Library and matching header: $PSScriptRoot\build\$Platform\$Configuration"
