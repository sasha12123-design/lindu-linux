# lindu linux

Собственный дистрибутив Linux на основе **Arch Linux** с собственным
композиционным оконным менеджером **lindu-wm**.

## Что внутри

| Компонент            | Описание                                              |
|----------------------|-------------------------------------------------------|
| База                 | Arch Linux (rolling release)                          |
| Ядро                 | linux (стандартное ядро Arch)                         |
| Оконная система      | Xorg (X11)                                            |
| Оконный менеджер     | `lindu-wm` — собственный тайловый WM написан на C/Xlib|
| Панель/статус        | встроенная в `lindu-wm`, показывает часы, имя хоста и раскладки |
| Запуск               | autologin на tty1, `startx` автоматически             |
| Пользователь         | `lindu` (пароль: `lindu`)                            |
| Формат поставки      | Live ISO (EFI + BIOS) через `archiso`                |
| Установка            | `lindu-install-gtk` (граф.) и `lindu-install` (CLI)   |

## Установка на диск (основная система)

```bash
lindu-install-gtk    # графический мастер (с рабочего стола)
sudo lindu-install   # текстовая версия
```

Мастер сам сделает разметку (GPT, GRUB для UEFI/BIOS), `pacstrap`,
настроит локаль, пользователя, сервисы, загрузчик и среду lindu.
Подробности: [docs/INSTALL.md](docs/INSTALL.md).

## Структура репозитория

```
rom/
├── wm/          — исходники оконного менеджера (C)
├── installer/   — установщики: lindu-install (bash) и lindu-install-gtk (Python/Gtk3)
├── profile/     — профиль archiso (образ системы и live-ISO)
├── configs/     — конфиги окружения (dotfiles)
├── build/       — скрипты сборки ISO, Dockerfile
├── docs/        — документация
└── Makefile     — точка входа
```

## Быстрый старт

**Облачная сборка (ничего не нужно ставить):** fork репозитория → вкладка
**Actions → build-iso → Run workflow** → ISO в артефактах. См. также статус:

[![build-iso](https://github.com/<user>/lindu/actions/workflows/build-iso.yml/badge.svg)](https://github.com/<user>/lindu/actions/workflows/build-iso.yml)

Локально:

```bash
# на Linux (или WSL2):
make build

# сборка через Docker (работает и на Windows):
./build/build-in-docker.linux.sh     # bash
powershell -File build/build-in-docker.ps1   # Windows
```

Результат — ISO в `out/lindu-*.iso`. Подробности: [docs/BUILD.md](docs/BUILD.md).

## Горячие клавиши lindu-wm

| Клавиша        | Действие                       |
|----------------|--------------------------------|
| `Mod+Enter`    | терминал (alacritty)           |
| `Mod+d`        | rofi (запуск приложений)       |
| `Mod+j/k`      | переключение окна вниз/вверх   |
| `Mod+Shift+j/k`| переместить окно вниз/вверх    |
| `Mod+h/l`      | изменить размер мастера        |
| `Mod+t/m/f`    | раскладки: тайл/монокль/флоат  |
| `Mod+←/→`      | переключить монитор            |
| `Mod+Shift+←/→`| отправить окно на другой монитор |
| `Mod+Shift+c`  | закрыть окно                   |
| `Mod+1..9`     | переключить тег (рабочий стол) |
| `Mod+Shift+q`  | выйти из X                     |

`Mod` = `Super` (Windows).