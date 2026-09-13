#!/usr/bin/env bash
# remove from airootfs!
#
# Настраивает live-систему lindu linux во время сборки образа.
# Запускается pacman-хуком после установки всех пакетов.

set -euo pipefail

# ---------- пользователь ----------
if ! id lindu >/dev/null 2>&1; then
    useradd -m -G wheel,audio,video,storage,power,lp,rfkill -s /bin/bash lindu
fi
echo "lindu:lindu" | chpasswd
echo "root:lindu"  | chpasswd
chmod 755 /etc/skel

# ---------- консоль и локаль ----------
echo "KEYMAP=us" > /etc/vconsole.conf
printf "en_US.UTF-8 UTF-8\nru_RU.UTF-8 UTF-8\n" >> /etc/locale.gen || true
locale-gen >/dev/null 2>&1 || true

# ---------- сервисы ----------
systemctl enable NetworkManager  >/dev/null 2>&1 || true
systemctl enable sshd            >/dev/null 2>&1 || true
systemctl enable nscd            >/dev/null 2>&1 || true

# ---------- разрешения для панели / окон ----------
chmod +x /usr/local/bin/lindu-wm
chmod +x /etc/skel/.xinitrc

# ---------- брендинг os-release ----------
cat > /etc/os-release <<'EOSR'
NAME="lindu linux"
ID=lindu
PRETTY_NAME="lindu linux (Arch based)"
ANSI_COLOR="38;2;121;215;240"
HOME_URL="https://lindu.local"
BUG_REPORT_URL="https://lindu.local"
EOSR

exit 0