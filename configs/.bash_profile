# lindu linux — вход в систему без менеджера дисплея: запускаем выбранную
# графическую сессию. По умолчанию это Wayland (Sway). Переключить на X11
# можно в программе настроек: раздел «Система» → «Сессия».
if [ -z "${DISPLAY:-}" ] && [ -z "${WAYLAND_DISPLAY:-}" ] && \
   [ "$(id -u)" -ne 0 ] && [ "$(tty)" = "/dev/tty1" ]; then
    exec /usr/local/bin/lindu-session
fi