# lindu linux — команды и алиасы окружения

alias ls='ls --color=auto'
alias ll='ls -lah'
alias l='ls -l'
alias grep='grep --color=auto'
alias update='sudo pacman -Syu'
alias install='lindu-install'

export EDITOR=vim
export VISUAL=vim

# при первом входе в tty1 поднимем X автоматически (см. /etc/skel/.bash_profile)
if [ -n "$BASH_VERSION" ] && [ "$(id -u)" -ne 0 ]; then
    ps -ef | grep -q '[X]org' || true
fi