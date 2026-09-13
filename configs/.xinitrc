#!/bin/sh
# lindu linux — .xinitrc (окружение desktop)

# ресурсы и тема (rofi, alacritty, ...)
[ -f "$HOME/.Xresources" ] && xrdb -merge "$HOME/.Xresources"

# фон рабочего стола
if [ -f /usr/share/backgrounds/lindu.png ]; then
    feh --bg-scale /usr/share/backgrounds/lindu.png
else
    xsetroot -solid "#0b0e12"
fi

# раскладки: us + ru, переключение на CapsLock
setxkbmap -layout us,ru -option grp:caps_toggle

# аудио
pipewire >/dev/null 2>&1 &
pipewire-pulse >/dev/null 2>&1 &

# демон acpi (батарея)
acpid >/dev/null 2>&1 &

# папки пользователя
xdg-user-dirs-update >/dev/null 2>&1 &

# собственный оконный менеджер lindu
exec /usr/local/bin/lindu-wm