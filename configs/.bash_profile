# lindu linux — автозапуск X при входе без дисплея
if [ -z "$DISPLAY" ] && [ "$(id -u)" -ne 0 ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec startx
fi