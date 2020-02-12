#!/bin/bash

cp -rv ".local/share/vulkan/implicit_layer.d/"* "$HOME/.local/share/vulkan/implicit_layer.d/"
cp -v ".local/share/MangoHud/"* "$HOME/.local/share/MangoHud/"
if [[ ! -f "$HOME/.local/share/MangoHud/MangoHud.conf" ]]; then
    cp -v ".local/share/MangoHud/MangoHud.conf" "$HOME/.local/share/MangoHud/"
fi

sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud32.so|g" "$HOME/.local/share/vulkan/implicit_layer.d/mangohud32.json"
sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud.so|g" "$HOME/.local/share/vulkan/implicit_layer.d/mangohud64.json"
