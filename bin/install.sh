#!/bin/bash

cp -rv ".local" "$HOME/"
if [[ ! -f "$HOME/.config/MangoHud/MangoHud.conf" ]]; then
    cp -v ".config" "$HOME/"
fi

sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud32.so|g" "$HOME/.local/share/vulkan/implicit_layer.d/mangohud32.json"
sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud.so|g" "$HOME/.local/share/vulkan/implicit_layer.d/mangohud64.json"
