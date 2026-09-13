# Использование lindu linux (live-окружение)

## Вход в систему

Live-система входит в X автоматически:

```
пользователь: lindu
пароль:       lindu
```

Для root: `lindu` на tty1/в `su` — пароль `lindu`.

## Горячие клавиши

`Mod` = клавиша **Super** (Windows).

| Комбинация          | Действие                              |
|---------------------|---------------------------------------|
| `Mod+Enter`         | терминал (alacritty)                  |
| `Mod+d`             | запуск программ (rofi)                |
| `Mod+Shift+b`       | браузер (firefox)                     |
| `Mod+e`             | файловый менеджер (thunar)            |
| `Mod+p`             | скриншот всего экрана                 |
| `Mod+j` / `Mod+k`   | фокус со стеков окружения             |
| `Mod+Shift+j/k`     | переместить окно вниз/вверх           |
| `Mod+h` / `Mod+l`   | высота мастера -/+                    |
| `Mod+Shift+h/l`     | ширина окна -/+                       |
| `Mod+Left/Right`    | переключение монитора              |
| `Mod+Shift+Left/Right` | отправить окно на соседний монитор |
| `Mod+t` / `m` / `x` | раскладки: тайл / монокль / флоат     |
| `Mod+Space`         | плавающий режим окна                  |
| `Mod+f`             | полноэкранный режим                   |
| `Mod+Shift+c`       | закрыть окно                          |
| `Mod+1` … `Mod+9`   | переключить рабочий стол              |
| `Mod+Shift+1…9`     | отправить окно на рабочий стол        |
| `Mod+Ctrl+1…9`      | вернуть окно на рабочий стол          |
| `Mod+b`             | показать/скрыть панель                |
| `Mod+Shift+q`       | выход из X                            |

## Панель (встроена в lindu-wm)

Показывает: рабочие столы (`[1*] [2] 3`), раскладку и часы.
Цвета и высота настраиваются в `wm/config.h` (пункты `BARH`, `ACCENT`, `PADDING_*`).

## Раскладки клавиатуры

По умолчанию **US + RU**, переключение **CapsLock**.
Изменить: `setxkbmap -layout us,ru -option grp:alt_shift_toggle` (на лету)
или в `configs/.xinitrc`.

## Сеть

- Wi-Fi/проводная: `NetworkManager` уже запущен:
  ```bash
  nmcli device wifi list
  nmcli device wifi connect "SSID" password "пароль"
  ```
- Для статики: `nmcli connection modify ... ipv4.method manual ipv4.addresses ...`

## Установка пакетов

```bash
sudo pacman -Syy    # обновить базы
sudo pacman -S <пакет>
```

## Изменение оконного менеджера

Исходники живут в образе: `/usr/src/lindu-wm`. Правьте `config.h`,
пересоберите и перезапустите:

```bash
cd /usr/src/lindu-wm
vim config.h
make && sudo make install
# выйти из X (Mod+Shift+q) и снова зайти
```