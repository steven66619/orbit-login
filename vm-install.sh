#!/bin/bash
set -euo pipefail

DISK=${1:-/dev/sda}
HOSTNAME=${2:-orbit-test}
USER=${3:-orbit}
PASS=${4:-orbit}
TIMEZONE=${5:-UTC}
REPO=${6:-https://github.com/user/orbit-login}

echo "==> orbit-login VM installer"
echo "    Disk:      $DISK"
echo "    Hostname:  $HOSTNAME"
echo "    User:      $USER"
echo "    Timezone:  $TIMEZONE"
echo "    Repo:      $REPO"
echo

confirm() {
    printf "%s " "[Press Enter to continue or Ctrl-C to abort]"
    read -r _
}

# --- Partition ---
confirm "Partition $DISK ?"
echo "==> Partitioning $DISK ..."
parted -s "$DISK" mklabel gpt
parted -s "$DISK" mkpart primary fat32 1MiB 513MiB
parted -s "$DISK" set 1 esp on
parted -s "$DISK" mkpart primary ext4 513MiB 100%

# --- Format ---
echo "==> Formatting ..."
mkfs.fat -F32 "${DISK}1"
mkfs.ext4 -F "${DISK}2"

# --- Mount ---
echo "==> Mounting ..."
mount "${DISK}2" /mnt
mount --mkdir "${DISK}1" /mnt/boot

# --- Pacstrap ---
echo "==> Installing base system (pacstrap) ..."
pacstrap -K /mnt base base-devel linux linux-firmware \
    gcc make libx11 pam \
    xorg-server xorg-xauth \
    xterm sudo vim networkmanager openssh \
    man-db man-pages

# --- Fstab ---
echo "==> Generating fstab ..."
genfstab -U /mnt >> /mnt/etc/fstab

# --- Chroot commands ---
cat <<ARCH_EOF | arch-chroot /mnt
set -euo pipefail

# Timezone
ln -sf /usr/share/zoneinfo/$TIMEZONE /etc/localtime
hwclock --systohc

# Locale
sed -i 's/^#en_US.UTF-8 UTF-8/en_US.UTF-8 UTF-8/' /etc/locale.gen
locale-gen
echo "LANG=en_US.UTF-8" > /etc/locale.conf

# Hostname
echo "$HOSTNAME" > /etc/hostname

# Hosts
cat >> /etc/hosts << HOSTS_EOF
127.0.0.1   localhost
::1         localhost
127.0.1.1   $HOSTNAME.localdomain $HOSTNAME
HOSTS_EOF

# mkinitcpio
mkinitcpio -P

# Root pass
echo "root:${PASS}" | chpasswd

# User
useradd -m -G wheel "$USER"
echo "$USER:$PASS" | chpasswd
echo "%wheel ALL=(ALL:ALL) ALL" >> /etc/sudoers.d/99-wheel

# Bootloader (systemd-boot)
bootctl install
cat > /boot/loader/entries/arch.conf << BOOT_EOF
title   Arch Linux
linux   /vmlinuz-linux
initrd  /initramfs-linux.img
options root=PARTUUID=\$(blkid -s PARTUUID -o value ${DISK}2) rw
BOOT_EOF

# Network
systemctl enable NetworkManager
systemctl enable sshd

# User dotfiles
cat > /home/$USER/.xinitrc << XINIT_EOF
exec \$SHELL
XINIT_EOF
chown $USER:$USER /home/$USER/.xinitrc

ARCH_EOF

# --- Build orbit-login ---
echo "==> Building orbit-login ..."
pacman -S --noconfirm git
cd /mnt
git clone "$REPO" /mnt/opt/orbit-login
make -C /mnt/opt/orbit-login
make -C /mnt/opt/orbit-login install DESTDIR=/mnt

# --- PAM ---
cp /mnt/opt/orbit-login/pam/orbit-login /mnt/etc/pam.d/orbit-login

# --- systemd unit ---
# already installed by "make install"

# --- Enable display manager ---
arch-chroot /mnt systemctl enable orbitd.service

# --- Done ---
echo
echo "==> Done!"
echo "    Unmount and reboot:"
echo "    umount -R /mnt && reboot"
