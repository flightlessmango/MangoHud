#!/bin/bash

XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"

cd $(dirname "$0")
cp -rv ".local/share/MangoHud" "$XDG_DATA_HOME/"
cp -rv ".local/share/vulkan" "$XDG_DATA_HOME/"
if [[ ! -f "$XDG_CONFIG_HOME/MangoHud/MangoHud.conf" ]]; then
    cp -rv ".config/MangoHud" "$XDG_CONFIG_HOME/"
fi

sed -i "s|libMangoHud.so|$XDG_DATA_HOME/MangoHud/libMangoHud32.so|g" "$XDG_DATA_HOME/vulkan/implicit_layer.d/mangohud32.json"
sed -i "s|libMangoHud.so|$XDG_DATA_HOME/MangoHud/libMangoHud.so|g" "$XDG_DATA_HOME/vulkan/implicit_layer.d/mangohud64.json"
