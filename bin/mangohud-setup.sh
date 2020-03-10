install() {
    [ "$UID" -eq 0 ] || exec sudo bash "$0" "$@"
    tar -C / -xf MangoHud*.tar
    ldconfig
    echo "MangoHud Installed"
}

uninstall() {
    [ "$UID" -eq 0 ] || exec sudo bash "$0" "$@"
    rm -rfv "/usr/lib/MangoHud"
    rm -fv "/usr/share/vulkan/implicit_layer.d/mangohud.json"
    rm -fv "/etc/ld.so.conf.d/libmangohud.conf"
    rm -fv "/usr/bin/mangohud"
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