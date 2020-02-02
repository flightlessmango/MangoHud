#!/bin/bash

cp -r .local $HOME/
sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud32.so|g" $HOME/.local/share/vulkan/implicit_layer.d/mangohud32.json
sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud.so|g" $HOME/.local/share/vulkan/implicit_layer.d/mangohud64.json
