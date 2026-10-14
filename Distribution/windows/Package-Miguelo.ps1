param([Parameter(Mandatory)][string]$Workspace, [Parameter(Mandatory)][string]$Revision)
$ErrorActionPreference = 'Stop'
$bundle = Join-Path $Workspace 'build/windows-x64/VitaPlugHarmoHitMasterR1_artefacts/Release/VST3/VitaPlug HARMOHIT MASTER.vst3'
$binary = Join-Path $bundle 'Contents/x86_64-win/VitaPlug HARMOHIT MASTER.vst3'
if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw 'Missing Windows x64 bundle/binary' }
$bytes = [IO.File]::ReadAllBytes($binary)
if ($bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) { throw 'Not an MZ Windows binary' }
$offset = [BitConverter]::ToInt32($bytes, 0x3c)
if ([BitConverter]::ToUInt32($bytes,$offset) -ne 0x4550) { throw 'Missing PE signature' }
if ([BitConverter]::ToUInt16($bytes,$offset+4) -ne 0x8664) { throw 'Not PE x86_64' }
if ([BitConverter]::ToUInt16($bytes,$offset+24) -ne 0x20b) { throw 'Not PE32+' }
if (Test-Path -LiteralPath (Join-Path $bundle 'Contents/MacOS')) { throw 'macOS bundle rejected' }
$info = Get-Content (Join-Path $bundle 'Contents/Resources/moduleinfo.json') -Raw | ConvertFrom-Json
if ($info.Name -ne 'VitaPlug HARMOHIT MASTER' -or $info.Version -ne '0.2.0') { throw 'Unexpected VST3 product/version' }
$coreHash = (Get-FileHash (Join-Path $Workspace 'Source/DSP/Drive670Core.h')).Hash.ToLowerInvariant()
$postHash = (Get-FileHash (Join-Path $Workspace 'Source/DSP/Drive670PostGain.h')).Hash.ToLowerInvariant()
Write-Output "Checked-out core SHA256=$coreHash; post-gain SHA256=$postHash"
if ($coreHash -ne '556d2b923570b2e05f18374cae1c0a0e7af6b41eb7d9b269b6babb97bc1b8f7e' -or $postHash -ne '27f3ad4f99a220d47dcb5bdcd0aa581038138285dbb36e8942207b5f3bb0f8a3') { throw 'Approved 670 core hash mismatch' }
$asset = Join-Path $Workspace 'Resources/HARMOHIT/HarmoHitMasterApproved.png'
if ((Get-FileHash $asset).Hash.ToLowerInvariant() -ne '5ff9e764c6fd10db521e5747e60d1eca0d536e494399effd23d820614eece85d') { throw 'Approved panel changed' }
Add-Type -TypeDefinition @'
public static class EmbeddedBlobCheck {
 public static bool Contains(byte[] data, byte[] blob) {
  for (int i=0; i<=data.Length-blob.Length; ++i) {
   if(data[i]!=blob[0] || data[i+1]!=blob[1] || data[i+2]!=blob[2] || data[i+3]!=blob[3]) continue;
   int j=4; for(;j<blob.Length && data[i+j]==blob[j];++j) {}
   if(j==blob.Length) return true;
  } return false;
 }
}
'@
if (-not [EmbeddedBlobCheck]::Contains($bytes,[IO.File]::ReadAllBytes($asset))) { throw 'Approved PNG is not embedded in the PE binary' }
$validation = Join-Path $Workspace 'validation'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
$dumpbin = Get-ChildItem (Join-Path $vs 'VC/Tools/MSVC') -Filter dumpbin.exe -Recurse | Where-Object { $_.FullName -match 'Hostx64[\\/]x64[\\/]' } | Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw 'MSVC dumpbin not found' }
& $dumpbin.FullName /dependents $binary 2>&1 | Tee-Object (Join-Path $validation 'runtime-dependents.txt')
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed' }
@"
PE32+ machine 0x8664 (x86_64), Release VS2022/windows-2022.
VitaPlug HARMOHIT MASTER.vst3; version 0.2.0 / R2 DRIVE 670.
VST3 UID 250544335 verified by actual host; see CTest log.
Approved PNG present byte-for-byte inside PE; frozen core/post-gain SHA-256 verified.
Commit: $Revision
Steinberg validator: see CI evidence. Private LAB/WAV suites NOT run in Windows.
Pro Tools / Blue Cat PatchWork / physical listening NOT tested on this runner.
"@ | Set-Content (Join-Path $validation 'binary-validation.txt')

$stage = Join-Path $env:RUNNER_TEMP 'VitaPlug_HARMOHIT_MASTER_R2_Windows_x64_Miguelo'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $bundle -Destination (Join-Path $stage 'VitaPlug HARMOHIT MASTER.vst3') -Recurse
foreach ($name in @('README_INSTALACION.txt','README_PRUEBAS.txt')) { Copy-Item (Join-Path $Workspace "Distribution/windows/$name") $stage }
Copy-Item (Join-Path $Workspace 'Documentation/THIRD_PARTY_NOTICES.txt') $stage
foreach ($name in @('runtime-dependents.txt','binary-validation.txt','steinberg-validator.txt','test-scope.txt')) { Copy-Item (Join-Path $validation $name) $stage }
@"
VitaPlug HARMOHIT MASTER R2 — DRIVE 670 / v0.2.0
MIGUELOMENER EDITION identifica el paquete, no modifica el plugin.
Commit compilado: $Revision
Base main: f2e88eef2a28bcca1d547ad207af8f9da33f418b
Tag v0.2.0: ab739e823a29949af770c4af46294a47768db24d
JUCE 9.0.3: be29c81492b6151c8ea8d14c840e1311963b3a83
Core 670: $coreHash
Post gain: $postHash
Windows Server 2022 / Visual Studio 2022 / C++20 / Release x64.
CTest público: MasterTests, DriveTests, Public670Tests, VST3Smoke.
Equivalencia LAB/MASTER y WAV privados: NO ejecutados en Windows.
No se afirma equivalencia bit a bit macOS/Windows.
JUCE 9 Starter comercial, según la decisión del propietario documentada.
Código e imágenes VitaPlug: todos los derechos reservados; entrega a Miguelo para evaluación.
No se concede licencia general de redistribución. THIRD_PARTY_NOTICES.txt contiene los avisos completos.
Sin firma Authenticode; no es instalador ni AAX. Smoke musical Pro Tools/PatchWork pendiente.
"@ | Set-Content (Join-Path $stage 'VERSION_ORIGEN.txt')
$files = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName
$sums = foreach ($file in $files) { '{0}  {1}' -f (Get-FileHash $file.FullName).Hash.ToLowerInvariant(), ([IO.Path]::GetRelativePath($stage,$file.FullName).Replace('\','/')) }
$sums | Set-Content (Join-Path $stage 'SHA256SUMS.txt')
$archive = Join-Path $env:RUNNER_TEMP 'VitaPlug_HARMOHIT_MASTER_R2_Windows_x64_Miguelo.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive
$zipSum = Join-Path $env:RUNNER_TEMP 'SHA256SUMS_ZIP.txt'
('{0}  {1}' -f (Get-FileHash $archive).Hash.ToLowerInvariant(), [IO.Path]::GetFileName($archive)) | Set-Content $zipSum
"MIGUELO_ZIP=$archive" | Out-File $env:GITHUB_ENV -Append
"MIGUELO_ZIP_SHA=$zipSum" | Out-File $env:GITHUB_ENV -Append
Get-Content $zipSum
