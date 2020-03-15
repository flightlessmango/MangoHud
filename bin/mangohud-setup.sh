install() {
    rm -rf "$HOME/.local/share/MangoHud/"
    rm -f "$HOME/.local/share/vulkan/implicit_layer.d/"{mangohud32.json,mangohud64.json}
    [ "$UID" -eq 0 ] || exec sudo bash "$0" install
    tar -C / -xvhf MangoHud-package.tar
    echo "MangoHud Installed"
}

uninstall() {
    [ "$UID" -eq 0 ] || exec sudo bash "$0" uninstall
    rm -rfv "/usr/lib/mangohud"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json"
    rm -frv "/usr/share/doc/mangohud"
    rm -fv "/usr/bin/mangohud"
    rm -fv "/usr/bin/mangohud.x86"
}

for a in $@; do
    case $a in
        "install") install;;
        "uninstall") uninstall;;
        *)
            echo "Unrecognized command argument: $a"
            echo 'Accepted arguments: "install", "uninstall".'
    esac
done