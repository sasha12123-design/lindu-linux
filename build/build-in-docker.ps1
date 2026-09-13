# lindu linux - сборка ISO через Docker Desktop (Windows PowerShell)
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

Write-Host "==> Сборка образа сборщика"
docker build -f (Join-Path $PSScriptRoot "Dockerfile") -t lindu-builder $Root

Write-Host "==> Сборка ISO (10-20 минут)"
New-Item -ItemType Directory -Force -Path (Join-Path $Root "out") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $Root "work") | Out-Null

docker run --rm --privileged `
    --entrypoint /usr/bin/bash `
    -v "$Root\out:/lindu-out" `
    -v "$Root\work:/lindu-work" `
    -e LINDU_OUT_DIR=/lindu-out `
    -e LINDU_WORK_DIR=/lindu-work `
    lindu-builder /work/build/build.sh

Write-Host ""
Write-Host "Готово! ISO в: $(Join-Path $Root 'out')"