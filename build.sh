#!/bin/bash
set -e

# Go into the lyra-pkgmngr folder
cd "$(dirname "$0")/lyra-pkgmngr"

# Ensure we’re root only when needed
if ! command -v paru &>/dev/null; then
    echo "[*] Installing paru..."
    sudo pacman -S --needed --noconfirm base-devel git
    git clone https://aur.archlinux.org/paru.git
    cd paru
    makepkg -si --noconfirm
    cd ..
    rm -rf paru
else
    echo "[✓] paru already installed."
fi

# Ask to install cjson
read -p "Install cjson dependency with paru? (y/N): " ans
if [[ "$ans" =~ ^[Yy]$ ]]; then
    paru -S cjson --noconfirm
else
    echo "[!] Skipping cjson installation."
fi

# Compile lyra.c
echo "[*] Compiling lyra..."
gcc lyra.c -o lyra -lcjson

# Copy binary to /usr/local/bin
echo "[*] Copying lyra to /usr/local/bin (requires sudo)"
sudo cp lyra /usr/local/bin/lyra

# Restrict permissions but keep it executable by everyone, maybe should change this but idk about perms too well.
sudo chmod 755 /usr/local/bin/lyra

echo "[✓] Lyra installed successfully and ready to use."
echo "[!] Use: 'sudo lyra', to see all commands."
