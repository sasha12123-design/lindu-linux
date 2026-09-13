# Сборка ISO lindu linux

ISO собирается стандартным инструментом **Arch Linux archiso**, но с одной
хитростью: профиль lindu накладывается поверх эталонного профиля `releng` из
установленного пакета `archiso`. Это гарантирует, что конфиги загрузчика
(grub/syslinux/systemd-boot) всегда совпадают с версией `mkarchiso` в системе
сборки.

## Вариант 1 — GitHub Actions (облако, без локального Linux)

Сборка полностью в облаке: на linux-раннере поднимается контейнер
`archlinux:latest`, ставится `archiso`, запускается `build/build.sh`.

- Push в `main`/`master` или тег `v*` — сборка автоматически.
- Вручную: вкладка **Actions → build-iso → Run workflow**.
- Готовый ISO — вкладка **Summary** (артефакт `lindu-iso`, хранится 14 дней).
- Теги `v*` дополнительно создают GitHub Release с прикреплённым ISO.

## Вариант 2 — Docker (рекомендуется, работает и на Windows)

```bash
# Linux / WSL2 / macOS (нужен Docker):
./build/build-in-docker.linux.sh

# Windows (нужен Docker Desktop + Linux-контейнеры):
powershell -ExecutionPolicy Bypass -File build/build-in-docker.ps1
```

Результат: `out/lindu-*.iso`.

Что происходит внутри: собирается контейнер на базе `archlinux:latest`,
ставится `archiso`, монтируются каталоги `out/` и `work/` для сохранения
результата, и запускается `build/build.sh`.

## Вариант 3 — нативно (Arch Linux или любая машина с pacman)

```bash
sudo pacman -S --needed archiso base-devel libx11 libxinerama
make build
```

`build/build.sh` автоматически:
1. доставит недостающие `archiso`, `gcc`, `libx11`;
2. скопирует releng-профиль из `/usr/share/archiso/configs/releng`;
3. наложит слой lindu (`profile/` + `configs/` + `wm/`);
4. соберёт оконный менеджер и установит его в образ через `DESTDIR`;
5. запустит `mkarchiso -v -w work/build -o out profile-lindu`.

## Переменные окружения

| Переменная        | По умолчанию  | Назначение              |
|-------------------|---------------|-------------------------|
| `LINDU_WORK_DIR`  | `work/`       | рабочая папка archiso   |
| `LINDU_OUT_DIR`   | `out/`        | куда кладётся ISO       |

## Требования

- ~10 ГБ свободного места (готовьтесь к ~2 ГБ кэша pacman + squashfs)
- Интернет (пакеты качаются из репозиториев Arch)
- Память: сборка не критична к RAM, но ~2 ГБ желательно

## Пробные запуски без виртуалки

```bash
sudo pacman -S --needed qemu-desktop edk2-ovmf
run_archiso -i out/lindu-*.iso
```

## Известные нюансы

- `profiledef.sh` содержит `iso_version="$(date ...)"`; при необходимости
  укажите фиксированную версию.
- Список пакетов: `profile/packages.x86_64`.
- Свой пользователь live — `profile/airootfs/usr/local/bin/lindu-setup.sh`.