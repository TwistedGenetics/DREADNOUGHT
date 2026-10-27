# SPDX-License-Identifier: GPL-3.0-only
# CI (smk37.yml): JieLi's Windows toolchain 2.5.3, the one M-VAVE's SMK37 SDK checks; prints its pi32 directory.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force C:\JL | Out-Null
& curl.exe --fail --location --retry 3 --silent --show-error --output C:\JL\2.5.3.exe https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe
if ($LASTEXITCODE -ne 0) { throw "download failed ($LASTEXITCODE)" }
$f = Get-Item C:\JL\2.5.3.exe
Write-Host "installer: $($f.Length) bytes, sha256 $((Get-FileHash $f -Algorithm SHA256).Hash)"
# (an Inno Setup installer: installed quietly into C:\JL\inst)
$p = Start-Process -FilePath C:\JL\2.5.3.exe -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/DIR=C:\JL\inst' -Wait -PassThru
Write-Host "installer: exit $($p.ExitCode)"
$clang = $null
foreach ($root in @('C:\JL\inst','C:\JL','C:\')) {
    $clang = Get-ChildItem -Recurse -Path $root -Filter clang.exe -ErrorAction SilentlyContinue -Depth 6 |
        Where-Object { $_.FullName -match 'pi32' -and (Test-Path (Join-Path (Split-Path -Parent $_.FullName) 'pi32v2-ld.exe')) } |
        Select-Object -First 1
    if ($clang) { break }
}
if (!$clang) {
    Get-ChildItem -Recurse C:\JL -Depth 3 -ErrorAction SilentlyContinue | Select-Object -First 60 | ForEach-Object { $_.FullName }
    throw 'no pi32 clang.exe (with pi32v2-ld.exe beside it) after the install'
}
$pi32 = Split-Path -Parent (Split-Path -Parent $clang.FullName)
"FM1_PI32_TOOLCHAIN=$pi32" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
Write-Host "pi32: $pi32"
