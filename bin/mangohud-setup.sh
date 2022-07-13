#!/usr/bin/env bash
OS_RELEASE_FILES=("/etc/os-release" "/usr/lib/os-release")
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
MANGOHUD_CONFIG_DIR="$XDG_CONFIG_HOME/MangoHud"
SU_CMD=$(command -v sudo || command -v doas || echo)

# doas requires a double dash if the command it runs will include any dashes,
# so append a double dash to the command
[[ $SU_CMD == *doas ]] && SU_CMD="$SU_CMD -- "

# Correctly identify the os-release file.
for os_release in ${OS_RELEASE_FILES[@]} ; do
    if [[ ! -e "${os_release}" ]]; then
        continue
    fi
    DISTRO=$(sed -rn 's/^ID(_LIKE)*=(.+)/\2/p' ${os_release} | sed 's/"//g')
done

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

mangohud_uninstall() {
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" uninstall
    rm -rfv "/usr/lib/mangohud"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.json"
    rm -frv "/usr/share/doc/mangohud"
    rm -fv "/usr/share/man/man1/mangohud.1"
    rm -fv "/usr/bin/mangohud"
    rm -fv "/usr/bin/mangohud.x86"
}

mangohud_install() {
    rm -rf "$HOME/.local/share/MangoHud/"
    rm -f "$HOME/.local/share/vulkan/implicit_layer.d/"{mangohud32.json,mangohud64.json}

    [ "$UID" -eq 0 ] || mangohud_config
    [ "$UID" -eq 0 ] || tar xf MangoHud-package.tar
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" install

    mangohud_uninstall

    DEFAULTLIB=lib32
    for i in $DISTRO; do
        case $i in
            *arch*)
            DEFAULTLIB=lib64
            ;;
        esac
    done

    echo DEFAULTLIB: $DEFAULTLIB
    /usr/bin/install -Dvm644 ./usr/lib/mangohud/lib64/libMangoHud.so /usr/lib/mangohud/lib64/libMangoHud.so
    /usr/bin/install -Dvm644 ./usr/lib/mangohud/lib64/libMangoHud_dlsym.so /usr/lib/mangohud/lib64/libMangoHud_dlsym.so
    /usr/bin/install -Dvm644 ./usr/lib/mangohud/lib32/libMangoHud.so /usr/lib/mangohud/lib32/libMangoHud.so
    /usr/bin/install -Dvm644 ./usr/lib/mangohud/lib32/libMangoHud_dlsym.so /usr/lib/mangohud/lib32/libMangoHud_dlsym.so
    /usr/bin/install -Dvm644 ./usr/share/vulkan/implicit_layer.d/MangoHud.json /usr/share/vulkan/implicit_layer.d/MangoHud.json
    /usr/bin/install -Dvm644 ./usr/share/man/man1/mangohud.1 /usr/share/man/man1/mangohud.1
    /usr/bin/install -Dvm644 ./usr/share/doc/mangohud/MangoHud.conf.example /usr/share/doc/mangohud/MangoHud.conf.example
    /usr/bin/install -vm755  ./usr/bin/mangohud /usr/bin/mangohud

    ln -sv $DEFAULTLIB /usr/lib/mangohud/lib

    # FIXME get the triplet somehow
    ln -sv lib64 /usr/lib/mangohud/x86_64
    ln -sv lib64 /usr/lib/mangohud/x86_64-linux-gnu
    ln -sv . /usr/lib/mangohud/lib64/x86_64
    ln -sv . /usr/lib/mangohud/lib64/x86_64-linux-gnu

    ln -sv lib32 /usr/lib/mangohud/i686
    ln -sv lib32 /usr/lib/mangohud/i386-linux-gnu
    ln -sv lib32 /usr/lib/mangohud/i686-linux-gnu

    mkdir -p /usr/lib/mangohud/tls
    ln -sv ../lib64 /usr/lib/mangohud/tls/x86_64
    ln -sv ../lib32 /usr/lib/mangohud/tls/i686

    # Some distros search in $prefix/x86_64-linux-gnu/tls/x86_64 etc instead
    if [ ! -e /usr/lib/mangohud/lib/i386-linux-gnu ]; then
        ln -sv ../lib32 /usr/lib/mangohud/lib/i386-linux-gnu
    fi
    if [ ! -e /usr/lib/mangohud/lib/i686-linux-gnu ]; then
        ln -sv ../lib32 /usr/lib/mangohud/lib/i686-linux-gnu
    fi
    if [ ! -e /usr/lib/mangohud/lib/x86_64-linux-gnu ]; then
        ln -sv ../lib64 /usr/lib/mangohud/lib/x86_64-linux-gnu
    fi

    # $LIB can be "lib/tls/x86_64"?
    ln -sv ../tls /usr/lib/mangohud/lib/tls

    rm -rf ./usr

    echo "MangoHud Installed"
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
