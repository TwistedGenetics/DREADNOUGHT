# SPDX-License-Identifier: GPL-3.0-only
# CI (smk37.yml): JieLi's Windows toolchain 2.5.3, the one M-VAVE's SMK37 SDK checks; prints its pi32 directory.
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force C:\JL | Out-Null
& curl.exe --fail --location --retry 3 --silent --show-error --output C:\JL\2.5.3.exe https://jl-update.oss-cn-shenzhen.aliyuncs.com/2.5.3.exe
if ($LASTEXITCODE -ne 0) { throw "download failed ($LASTEXITCODE)" }
$f = Get-Item C:\JL\2.5.3.exe
Write-Host "installer: $($f.Length) bytes, sha256 $((Get-FileHash $f -Algorithm SHA256).Hash)"
& 7z x -y -oC:\JL\x C:\JL\2.5.3.exe > C:\JL\7z.log
Write-Host "7z: $LASTEXITCODE"
$clang = Get-ChildItem -Recurse -Path C:\JL\x -Filter clang.exe -ErrorAction SilentlyContinue | Where-Object { $_.FullName -match 'pi32' } | Select-Object -First 1
if (!$clang) {
    Get-Content C:\JL\7z.log -Tail 15
    Get-ChildItem -Recurse C:\JL\x -ErrorAction SilentlyContinue | Select-Object -First 40 | ForEach-Object { $_.FullName }
    throw 'no pi32 clang.exe found in the installer'
}
$pi32 = Split-Path -Parent (Split-Path -Parent $clang.FullName)
"FM1_PI32_TOOLCHAIN=$pi32" | Out-File -Append -Encoding utf8 $env:GITHUB_ENV
Write-Host "pi32: $pi32"
