#!/bin/bash    
OS_RELEASE_FILES=("/etc/os-release" "/usr/lib/os-release")
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_DIR="$XDG_DATA_HOME/MangoHud"
LAYER="build/release/usr/share/vulkan/implicit_layer.d/mangohud.json"
INSTALL_DIR="build/package/MangoHud"
IMPLICIT_LAYER_DIR="$XDG_DATA_HOME/vulkan/implicit_layer.d"
VERSION=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')

# Correctly identify the os-release file.
for os_release in ${OS_RELEASE_FILES[@]} ; do
    if [[ ! -e "${os_release}" ]]; then
        continue
    fi
    DISTRO=$(sed 1q ${os_release} | sed 's/NAME=//g' | sed 's/"//g')
done

dependencies() {
    if [[ ! -f build/release/usr/lib64/libMangoHud.so ]]; then
        missing_deps() {
            echo "# Missing dependencies:$INSTALL"
            read -rp "Do you wish the script to install these packages? [y/N]" PERMISSION
            case "$PERMISSION" in
                "y"|"Y") echo "Attempting to install missing packages"; sleep 0.5;;
                *) echo "Continuing with missing dependencies"; sleep 1;;
            esac
        }
        install() {
            for i in $(eval echo $DEPS); do
                $MANAGER_QUERY "$i" &> /dev/null
                if [[ $? == 1 ]]; then
                    INSTALL="$INSTALL""$i "
                fi
            done
            if [[ ! -z "$INSTALL" ]]; then
                missing_deps
                if [[ "$PERMISSION" == "Y" || "$PERMISSION" == "y" ]]; then
                    sudo $MANAGER_INSTALL $INSTALL
                fi
            fi
        }
        echo "# Checking Dependencies"
        
        case $DISTRO in
            "Arch Linux"|"Manjaro")
                MANAGER_QUERY="pacman -Q"
                MANAGER_INSTALL="pacman -S"
                DEPS="{gcc,meson,pkgconf,python-mako,glslang,libglvnd,lib32-libglvnd}"
                install
            ;;
            "Fedora")
                MANAGER_QUERY="dnf list installed"
                MANAGER_INSTALL="dnf install"
                DEPS="{meson,gcc,g++,libX11-devel,glslang,python-mako,mesa-libGL-devel}"
                install

                unset INSTALL
                DEPS="{glibc-devel.i686,libstdc++-devel.i686,libX11-devel.i686}"
                install
            ;;
            *"buntu"|"Linux Mint"|"Debian"|"Zorin OS"|"Pop!_OS")
                MANAGER_QUERY="dpkg-query -s"
                MANAGER_INSTALL="apt install"
                DEPS="{gcc,g++,gcc-multilib,g++-multilib,ninja-build,python3-pip,python3-setuptools,python3-wheel,pkg-config,mesa-common-dev,libx11-dev:i386}"
                install
                
                if [[ $(sudo pip3 show meson; echo $?) == 1 || $(sudo pip3 show mako; echo $?) == 1 ]]; then
                    sudo pip3 install meson mako
                fi
                if [[ ! -f /usr/local/bin/glslangValidator ]]; then
                    wget https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
                    unzip glslang-master-linux-Release.zip bin/glslangValidator
                    sudo install -m755 bin/glslangValidator /usr/local/bin/
                    rm bin/glslangValidator glslang-master-linux-Release.zip
                fi
            ;;
            "Solus")
                unset MANAGER_QUERY
                unset DEPS
                MANAGER_INSTALL="eopkg it"

                local packages=("mesalib-32bit-devel" "glslang" "libstdc++-32bit" "glibc-32bit-devel" "mako")

                # eopkg doesn't emit exit codes properly, so use the python API to find if a package is installed.
                for package in ${packages[@]}; do
                    python -c "import pisi.db; import sys; idb = pisi.db.installdb.InstallDB(); sys.exit(0 if idb.has_package(\"${package}\") else 1)"
                    if [[ $? -ne 0 ]]; then
                        INSTALL="${INSTALL}""${package} "
                    fi
                done

                # likewise, ensure the whole system.devel component is satisfied
                python -c "import pisi.db; import sys; idb = pisi.db.installdb.InstallDB(); cdb = pisi.db.componentdb.ComponentDB(); mpkgs = [x for x in cdb.get_packages('system.devel') if not idb.has_package(x)]; sys.exit(0 if len(mpkgs) == 0 else 1)"

                if [[ $? -ne 0 ]]; then
                    INSTALL="${INSTALL}""-c system.devel "
                fi
                install
                ;;
            *)
                echo "# Unable to find distro information!"
                echo "# Attempting to build regardless"
        esac
    fi
}

configure() {
    dependencies
    git submodule update --init --depth 50
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        meson build/meson64 --libdir lib64 --prefix "$PWD/build/release/usr"
    fi
    if [[ ! -f "build/meson32/build.ninja" ]]; then
        export CC="gcc -m32"
        export CXX="g++ -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH_32}"
        export LLVM_CONFIG="/usr/bin/llvm-config32"
        meson build/meson32 --libdir lib32 --prefix "$PWD/build/release/usr"
    fi
}

build() {
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        configure
    fi
    ninja -C build/meson32 install
    ninja -C build/meson64 install
}

package() {
    LIB="build/release/usr/lib64/libMangoHud.so"
    LIB32="build/release/usr/lib32/libMangoHud.so"
    if [[ ! -f "$LIB" || "$LIB" -ot "build/meson64/src/libMangoHud.so" ]]; then
        build
    fi
    mkdir -p "$INSTALL_DIR/.local/share/"{MangoHud,vulkan/implicit_layer.d}
    mkdir -p "$INSTALL_DIR/.config/MangoHud"

    cp "$LIB32" "$INSTALL_DIR/.local/share/MangoHud/libMangoHud32.so"
    cp "$LIB" "$INSTALL_DIR/.local/share/MangoHud/libMangoHud.so"
    cp "$LAYER" "$INSTALL_DIR/.local/share/vulkan/implicit_layer.d/mangohud64.json"
    cp "$LAYER" "$INSTALL_DIR/.local/share/vulkan/implicit_layer.d/mangohud32.json"
    cp --preserve=mode "bin/install.sh" "build/package/MangoHud/install.sh"
    cp --preserve=mode "bin/run-mangohud-gl.sh" "build/package/MangoHud/run-mangohud-gl.sh"
    cp "bin/MangoHud.conf" "$INSTALL_DIR/.config/MangoHud/MangoHud.conf"
    cp "bin/MangoHud.conf" "$INSTALL_DIR/.local/share/MangoHud/MangoHud.conf"
    sed -i "s|64bit|32bit|g" "$INSTALL_DIR/.local/share/vulkan/implicit_layer.d/mangohud32.json"

    tar -C build/package -cpzf "build/MangoHud-$VERSION.tar.gz" .
}

install() {
    PKG="build/MangoHud-$VERSION.tar.gz"
    if [[ ! -f "$PKG" || "$PKG" -ot "build/meson64/src/libMangoHud.so" ]]; then
        package
    fi
    if [[ -f "$XDG_CONFIG_HOME/MangoHud/MangoHud.conf" ]]; then
        tar xzf "build/MangoHud-$VERSION.tar.gz" -C "$XDG_DATA_HOME/" "./MangoHud/.local/share/"{MangoHud,vulkan} --strip-components=4
    else
        tar xzf "build/MangoHud-$VERSION.tar.gz" -C "$XDG_DATA_HOME/" "./MangoHud/.local/share/"{MangoHud,vulkan} --strip-components=4
        tar xzf "build/MangoHud-$VERSION.tar.gz" -C "$XDG_CONFIG_HOME/" "./MangoHud/.config/MangoHud" --strip-components=3
    fi
    sed -i "s|libMangoHud.so|$XDG_DATA_HOME/MangoHud/libMangoHud32.so|g" "$XDG_DATA_HOME/vulkan/implicit_layer.d/mangohud32.json"
    sed -i "s|libMangoHud.so|$XDG_DATA_HOME/MangoHud/libMangoHud.so|g" "$XDG_DATA_HOME/vulkan/implicit_layer.d/mangohud64.json"
}

clean() {
    rm -rf "build"
}

uninstall() {
    rm -rfv "$XDG_DATA_HOME/MangoHud"
    rm -fv "$IMPLICIT_LAYER_DIR"/{mangohud64,mangohud32}.json
}

for a in $@; do
    case $a in
        "") build;;
        "pull") git pull;;
        "configure") configure;;
        "build") build;;
        "package") package;;
        "install") install;;
        "clean") clean;;
        "uninstall") uninstall;;
        *)
            echo "Unrecognized command argument: $a"
            echo 'Accepted arguments: "pull", "configure", "build", "package", "install", "clean", "uninstall".'
    esac
done
