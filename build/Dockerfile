# lindu linux — сборочная среда (Arch Linux)
FROM archlinux:latest

RUN pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm --needed archiso

WORKDIR /work

COPY . /work

ENTRYPOINT ["/work/build/build.sh"]