#!/bin/sh
# lindu linux — .xinitrc (окружение desktop)

# ресурсы и тема (rofi, alacritty, ...)
[ -f "$HOME/.Xresources" ] && xrdb -merge "$HOME/.Xresources"

# раскладки: us + ru, переключение на CapsLock
setxkbmap -layout us,ru -option grp:caps_toggle

# аудио
pipewire >/dev/null 2>&1 &
pipewire-pulse >/dev/null 2>&1 &

# демон acpi (батарея)
acpid >/dev/null 2>&1 &

# папки пользователя
xdg-user-dirs-update >/dev/null 2>&1 &

# панель Windows-стиля (таскбар снизу)
tint2 >/dev/null 2>&1 &

# GUI-приложения от root (установщик via pkexec) подключаются к :0 без пароля X
xhost +local: >/dev/null 2>&1

# агент polkit — диалог "введите пароль" для GUI-приложений
/usr/lib/polkit-gnome/polkit-gnome-authentication-agent-1 >/dev/null 2>&1 &

# фон рабочего стола
if [ -f /usr/share/backgrounds/lindu.png ]; then
    feh --bg-scale /usr/share/backgrounds/lindu.png
else
    xsetroot -solid "#0b0e12"
fi

# первый вход в live-сессию: показываем установщик на рабочем столе
if [ ! -f "$HOME/.lindu-first-run" ]; then
    touch "$HOME/.lindu-first-run"
    ( sleep 4; /usr/local/bin/lindu-install-gtk >/dev/null 2>&1 & )
fi

# собственный оконный менеджер lindu
exec /usr/local/bin/lindu-wm