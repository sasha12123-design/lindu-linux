#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="lindu"
iso_label="LINDU_$(date +%Y%m)"
iso_publisher="lindu linux <https://lindu.local>"
iso_application="lindu linux - собственный дистрибутив на базе Arch"
iso_version="$(date +%Y.%m.%d)"
install_dir="arch"
buildmodes=('iso')
bootmodes=('bios.syslinux' 'uefi.systemd-boot')
arch="x86_64"
pacman_conf="pacman.conf"
airootfs_image_type="squashfs"
airootfs_image_tool_options=('-comp' 'zstd' '-b' '1M')
file_permissions=(
  ["/etc/shadow"]="0:0:400"
  ["/root"]="0:0:750"
  ["/root/.automated_script.sh"]="0:0:755"
  ["/root/.gnupg"]="0:0:700"
  ["/etc/skel"]="0:0:755"
  ["/usr/local/bin/lindu-wm"]="0:0:755"
  ["/usr/local/bin/lindu-session"]="0:0:755"
  ["/usr/local/bin/lindu-session-wayland"]="0:0:755"
  ["/usr/local/bin/lindu-session-x11"]="0:0:755"
  ["/usr/local/bin/lindu-setup.sh"]="0:0:755"
  ["/usr/local/bin/lind""u-wifi"]="0:0:755"
  ["/usr/local/bin/lind""u-settings"]="0:0:755"
  ["/usr/local/bin/lindu-install"]="0:0:755"
  ["/usr/local/bin/lindu-install-gtk"]="0:0:755"
  ["/usr/local/bin/lindu-install-report"]="0:0:755"
  ["/usr/lib/lindu"]="0:0:755"
  ["/usr/lib/lindu/lindu_install_report.py"]="0:0:644"
  ["/usr/src/lindu-wm"]="0:0:755"
)