#!/usr/bin/env bash
# shellcheck disable=SC2034

iso_name="lindu"
iso_label="LINDU_$(date +%Y%m)"
iso_publisher="lindu linux <https://lindu.local>"
iso_application="lindu linux - собственный дистрибутив на базе Arch"
iso_version="$(date +%Y.%m.%d)"
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
  ["/usr/local/bin/lindu-setup.sh"]="0:0:755"
  ["/usr/src/lindu-wm"]="0:0:755"
)