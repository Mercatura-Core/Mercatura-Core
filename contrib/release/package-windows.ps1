# Copyright (c) 2026 The Mercatura Core developers
# Distributed under the MIT software license, see the accompanying file COPYING.
param([string]$Build = 'work/build', [string]$Output = 'work/artifacts')
$ErrorActionPreference = 'Stop'
function Check-Exit { if ($LASTEXITCODE -ne 0) { throw "Native command failed: $LASTEXITCODE" } }
$stage = Join-Path (Resolve-Path $Output) 'Mercatura-Core-v0.1.2-testnet'
if (Test-Path $stage) { throw 'Package staging must start empty' }
New-Item -ItemType Directory $stage | Out-Null
$bin = Join-Path $Build 'bin/Release'
foreach ($name in @('mercatura-qt', 'mercaturad', 'mercatura-cli', 'mercatura-gpu-check')) {
    Copy-Item "$bin/$name.exe" $stage
}
# vcpkg app-local deployment supplies dependent non-Qt libraries. windeployqt
# supplies Qt plugins and compiler runtime; never redistribute a GPU driver/ICD.
Get-ChildItem "$bin/*.dll" | Where-Object Name -NotMatch '^(OpenCL|nvcuda)\.dll$' | Copy-Item -Destination $stage
$qt = Get-ChildItem "$Build/vcpkg_installed/x64-windows/tools" -Filter windeployqt.exe -Recurse | Select-Object -First 1
if (!$qt) { throw 'Missing windeployqt' }
& $qt.FullName --release --compiler-runtime --no-translations --no-system-d3d-compiler --dir $stage "$stage/mercatura-qt.exe"
Check-Exit
if (!(Test-Path "$stage/platforms/qwindows.dll")) { throw 'Qt Windows platform plugin missing' }
if (Test-Path "$stage/OpenCL.dll") { throw 'OpenCL must be provided by the system AMD driver' }
Copy-Item 'COPYING' "$stage/COPYING.txt"
Copy-Item 'doc/release-notes/release-notes-0.1.2-testnet.md' "$stage/RELEASE-NOTES.md"
Copy-Item 'doc/release-v0.1.2-testnet.md' "$stage/BUILD-AND-BETA.md"
Copy-Item "$Output/windows-build.json" "$stage/build.json"
New-Item -ItemType Directory "$stage/licenses" | Out-Null
$licenses = Get-ChildItem $env:CUDA_PATH -Recurse -File | Where-Object Name -Match '^(EULA|LICENSE)'
if (!$licenses) { throw 'CUDA redistribution notices missing' }
$i = 0
foreach ($file in $licenses) { Copy-Item $file.FullName "$stage/licenses/CUDA-$i-$($file.Name)"; $i++ }
Get-ChildItem "$Build/vcpkg_installed/x64-windows/share" -Directory | ForEach-Object {
    if (Test-Path "$($_.FullName)/copyright") { Copy-Item "$($_.FullName)/copyright" "$stage/licenses/$($_.Name)-copyright" }
}
foreach ($name in @('mercaturad', 'mercatura-cli')) {
    $version = & "$stage/$name.exe" -version
    Check-Exit
    if (($version -join "`n") -notmatch 'v0.1.2') { throw 'Packaged version mismatch' }
}
# Fail on non-system imported DLLs absent from the distribution. dumpbin must
# run in the selected VS2022 environment. Windows API-set DLLs are OS supplied.
$system = Join-Path $env:SystemRoot 'System32'
$all = Get-ChildItem $stage -Recurse -File | Where-Object Extension -In @('.exe', '.dll')
foreach ($file in $all) {
    $imports = & dumpbin /dependents $file.FullName
    Check-Exit
    foreach ($line in $imports) {
        if ($line -match '^\s+([\w.-]+\.dll)\s*$') {
            $name = $Matches[1]
            if ($name -notmatch '^(api-ms-|ext-ms-)' -and !(Test-Path "$stage/$name") -and !(Test-Path "$system/$name")) {
                throw "Unresolved distribution dependency: $($file.Name) -> $name"
            }
        }
    }
}
$portable = Join-Path (Resolve-Path $Output) 'Mercatura-Core-v0.1.2-testnet-win64.zip'
Compress-Archive -Path "$stage/*" -DestinationPath $portable
$files = Get-ChildItem $stage -Recurse -File
$remove = Join-Path (Resolve-Path $Output) 'uninstall-files.nsh'
$lines = @($files | ForEach-Object { 'Delete "$INSTDIR\' + $_.FullName.Substring($stage.Length + 1) + '"' })
$lines += @(Get-ChildItem $stage -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object { 'RMDir "$INSTDIR\' + $_.FullName.Substring($stage.Length + 1) + '"' })
$lines | Set-Content $remove
$installer = Join-Path (Resolve-Path $Output) 'Mercatura-Core-v0.1.2-testnet-win64-setup.exe'
& 'C:/Program Files (x86)/NSIS/makensis.exe' "/DSTAGE=$stage" "/DOUTPUT=$installer" "/DREMOVE_FILES=$remove" contrib/release/windows-installer.nsi
Check-Exit
# Install, compare every shipped byte, verify shortcut, then uninstall in an
# isolated path. Never run the wallet GUI or touch a pre-existing installation.
$installed = Join-Path $env:RUNNER_TEMP 'mercatura-installer-smoke'
if (Test-Path $installed) { throw 'Installer smoke destination already exists' }
$process = Start-Process -FilePath $installer -ArgumentList @('/S', "/D=$installed") -Wait -PassThru
if ($process.ExitCode -ne 0) { throw 'Silent installer failed' }
foreach ($file in $files) {
    $relative = $file.FullName.Substring($stage.Length + 1)
    if ((Get-FileHash $file.FullName).Hash -ne (Get-FileHash "$installed/$relative").Hash) { throw "Installer mismatch: $relative" }
}
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut((Join-Path ([Environment]::GetFolderPath('Programs')) 'Mercatura Core Testnet.lnk'))
if ($shortcut.Arguments -ne '-testnet' -or $shortcut.TargetPath -ne "$installed\mercatura-qt.exe") { throw 'Wrong testnet shortcut' }
# NSIS uninstaller normally spawns a temporary copy. _?= keeps this process in
# place, so Wait observes completed cleanup without sleeps.
$process = Start-Process "$installed/uninstall.exe" -ArgumentList @('/S', "_?=$installed") -Wait -PassThru
if ($process.ExitCode -ne 0) { throw 'Silent uninstall failed' }
Remove-Item "$installed/uninstall.exe" -ErrorAction SilentlyContinue
if (Get-ChildItem $installed -Recurse -File -ErrorAction SilentlyContinue) { throw 'Uninstall left distributed files' }
$files | ForEach-Object { $_.FullName.Substring($stage.Length + 1) } | Set-Content "$Output/windows-contents.txt"
Remove-Item $stage -Recurse
