#!/usr/bin/env bash
# lindu linux — сборка live ISO
#
# Стратегия: берём эталонный releng-профиль из установленного пакета archiso
# (гарантирует совпадение с версией mkarchiso), кладём поверх слой lindu,
# собираем наш оконный менеджер и запускаем mkarchiso.
#
# Запуск:  ./build/build.sh      (требуется root или sudo)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

PROFILE_SRC="$ROOT/profile"
CONFIGS_DIR="$ROOT/configs"
WM_DIR="$ROOT/wm"

WORK_ROOT="${LINDU_WORK_DIR:-$ROOT/work}"
OUT_DIR="${LINDU_OUT_DIR:-$ROOT/out}"
STAGE_DIR="$WORK_ROOT/profile-lindu"
BUILD_WORK="$WORK_ROOT/build"

if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
    exec sudo -E bash "$0" "$@"
fi

echo "==> lindu linux: проверка инструментов"

if ! command -v mkarchiso >/dev/null 2>&1; then
    echo "    [archiso отсутствует] устанавливаю..."
    pacman -S --noconfirm --needed archiso
fi

# компилятор и заголовки для сборки менеджера
if ! command -v gcc >/dev/null 2>&1 || [ ! -f /usr/include/X11/Xlib.h ] || [ ! -f /usr/include/X11/extensions/Xinerama.h ]; then
    echo "    [gcc/libX11 отсутствуют] устанавливаю..."
    pacman -S --noconfirm --needed base-devel libx11 libxinerama
fi

RELENG_PROFILE="/usr/share/archiso/configs/releng"
[ -d "$RELENG_PROFILE" ] || {
    echo "ОШИБКА: не найден эталонный профиль $RELENG_PROFILE" >&2
    exit 1
}

echo "==> Подготовка профиля lindu (на основе releng)"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
cp -a "$RELENG_PROFILE/." "$STAGE_DIR/"

# наш профиль поверх эталонного
cp -f "$PROFILE_SRC/profiledef.sh"       "$STAGE_DIR/"
cp -f "$PROFILE_SRC/pacman.conf"         "$STAGE_DIR/"
cp -f "$PROFILE_SRC/packages.x86_64"     "$STAGE_DIR/"

# загрузчик (брендинг меню)
mkdir -p "$STAGE_DIR/grub" "$STAGE_DIR/syslinux" "$STAGE_DIR/efiboot/loader/entries"
cp -f "$PROFILE_SRC/grub/grub.cfg"                           "$STAGE_DIR/grub/"
cp -f "$PROFILE_SRC/syslinux/archiso_sys-linux.cfg"          "$STAGE_DIR/syslinux/"
cp -f "$PROFILE_SRC/efiboot/loader/entries/01-archiso-linux.conf"       "$STAGE_DIR/efiboot/loader/entries/"
cp -f "$PROFILE_SRC/efiboot/loader/entries/02-archiso-speech-linux.conf" "$STAGE_DIR/efiboot/loader/entries/"

# подстановка метки тома в конфигах загрузчика
LABEL="LINDU_$(date +%Y%m)"
sed -i "s|LINDU_LABEL|$LABEL|g" \
    "$STAGE_DIR/grub/grub.cfg" \
    "$STAGE_DIR/syslinux/archiso_sys-linux.cfg" \
    "$STAGE_DIR/efiboot/loader/entries/01-archiso-linux.conf" \
    "$STAGE_DIR/efiboot/loader/entries/02-archiso-speech-linux.conf"
sed -i "s|^iso_label=.*|iso_label=\"$LABEL\"|" "$STAGE_DIR/profiledef.sh"
echo "    метка тома: $LABEL"

# airootfs: база releng + наши файлы + dotfiles пользователя
cp -a "$PROFILE_SRC/airootfs/." "$STAGE_DIR/airootfs/"
mkdir -p "$STAGE_DIR/airootfs/etc/skel"
cp -a "$CONFIGS_DIR/." "$STAGE_DIR/airootfs/etc/skel/"

# исходники оконного менеджера попадают в образ (для изучения/пересборки)
mkdir -p "$STAGE_DIR/airootfs/usr/src/lindu-wm"
cp -a "$WM_DIR/." "$STAGE_DIR/airootfs/usr/src/lindu-wm/"

# установщик lindu linux (CLI + GTK)
mkdir -p "$STAGE_DIR/airootfs/usr/local/bin"
cp -f "$ROOT/installer/lindu-install"     "$STAGE_DIR/airootfs/usr/local/bin/lindu-install"
cp -f "$ROOT/installer/lindu-install-gtk" "$STAGE_DIR/airootfs/usr/local/bin/lindu-install-gtk"
chmod 0755 "$STAGE_DIR/airootfs/usr/local/bin/lindu-install" \
           "$STAGE_DIR/airootfs/usr/local/bin/lindu-install-gtk"

echo "==> Сборка lindu-wm (C/Xlib)"
make -C "$WM_DIR" clean >/dev/null 2>&1 || true
make -C "$WM_DIR" PREFIX=/usr/local DESTDIR="$STAGE_DIR/airootfs" install

echo "==> mkarchiso в процессе... (10-20 минут, требуется интернет)"
mkdir -p "$OUT_DIR" "$BUILD_WORK"
mkarchiso -v -w "$BUILD_WORK" -o "$OUT_DIR" "$STAGE_DIR"

echo
echo "Готово! ISO лежит в: $OUT_DIR"
ls -lh "$OUT_DIR"/*.iso | tail -n 1