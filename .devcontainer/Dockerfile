FROM oblique/archlinux-yay
RUN pacman -Syu --noconfirm

# C toolchain and other
RUN pacman -S --noconfirm gcc make cmake gdb valgrind
RUN sudo -u aur yay --noconfirm -S lcov
RUN sudo -u aur yay --noconfirm -S npm

WORKDIR /home/user/dev/
