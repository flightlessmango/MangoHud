#!/usr/bin/env bash
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
MANGOHUD_CONFIG_DIR="$XDG_CONFIG_HOME/MangoHud"
SU_CMD=$(command -v sudo || command -v doas)

# doas requires a double dash if the command it runs will include any dashes,
# so append a double dash to the command
[[ $SU_CMD == *doas ]] && SU_CMD="$SU_CMD -- "


mangohud_usage() {
    echo 'Accepted arguments: "install", "uninstall".'
}

mangohud_config() {
    mkdir -p "${MANGOHUD_CONFIG_DIR}"
    echo You can use the example configuration file from
    echo /usr/share/doc/mangohud/MangoHud.conf.example
    echo as a starting point by copying it to
    echo ${MANGOHUD_CONFIG_DIR}/MangoHud.conf
    echo
}

mangohud_install() {
    rm -rf "$HOME/.local/share/MangoHud/"
    rm -f "$HOME/.local/share/vulkan/implicit_layer.d/"{mangohud32.json,mangohud64.json}

    [ "$UID" -eq 0 ] || mangohud_config
    [ "$UID" -eq 0 ] || tar xf MangoHud-package.tar
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" install

    install -vm644 -D ./usr/lib/mangohud/lib32/libMangoHud.so /usr/lib/mangohud/lib32/libMangoHud.so
    install -vm644 -D ./usr/lib/mangohud/lib64/libMangoHud.so /usr/lib/mangohud/lib64/libMangoHud.so
    install -vm644 -D ./usr/lib/mangohud/lib32/libMangoHud_dlsym.so /usr/lib/mangohud/lib32/libMangoHud_dlsym.so
    install -vm644 -D ./usr/lib/mangohud/lib64/libMangoHud_dlsym.so /usr/lib/mangohud/lib64/libMangoHud_dlsym.so
    install -vm644 -D ./usr/share/vulkan/implicit_layer.d/MangoHud.x86.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86.json
    install -vm644 -D ./usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json
    install -vm644 -D ./usr/share/doc/mangohud/MangoHud.conf.example /usr/share/doc/mangohud/MangoHud.conf.example

    install -vm755 ./usr/bin/mangohud.x86 /usr/bin/mangohud.x86
    install -vm755 ./usr/bin/mangohud /usr/bin/mangohud

    echo "MangoHud Installed"
}

mangohud_uninstall() {
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" uninstall
    rm -rfv "/usr/lib/mangohud"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json"
    rm -frv "/usr/share/doc/mangohud"
    rm -fv "/usr/bin/mangohud"
    rm -fv "/usr/bin/mangohud.x86"
}

for a in $@; do
    case $a in
        "install") mangohud_install;;
        "uninstall") mangohud_uninstall;;
        *)
            echo "Unrecognized command argument: $a"
            mangohud_usage
    esac
done

if [ -z $@ ]; then
    mangohud_usage
fi
