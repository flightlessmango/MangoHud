#!/usr/bin/env bash
set -e

OS_RELEASE_FILES=("/etc/os-release" "/usr/lib/os-release")
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_DIR="$XDG_DATA_HOME/MangoHud"
CONFIG_DIR="$XDG_CONFIG_HOME/MangoHud"
LAYER="build/release/usr/share/vulkan/implicit_layer.d/mangohud.json"
INSTALL_DIR="build/package/"
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
            set +e
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
            set -e
        }
        echo "# Checking Dependencies"
        
        case $DISTRO in
            "Arch Linux"|"Manjaro Linux")
                MANAGER_QUERY="pacman -Q"
                MANAGER_INSTALL="pacman -S"
                DEPS="{gcc,meson,pkgconf,python-mako,glslang,libglvnd,lib32-libglvnd,libxnvctrl}"
                install
            ;;
            "Fedora")
                MANAGER_QUERY="dnf list installed"
                MANAGER_INSTALL="dnf install"
                DEPS="{meson,gcc,gcc-c++,libX11-devel,glslang,python3-mako,mesa-libGL-devel,libXNVCtrl-devel}"
                install

                unset INSTALL
                DEPS="{glibc-devel.i686,libstdc++-devel.i686,libX11-devel.i686}"
                install
            ;;
            *"buntu"|"Linux Mint"|"Debian"|"Zorin OS"|"Pop!_OS"|"elementary OS")
                MANAGER_QUERY="dpkg-query -s"
                MANAGER_INSTALL="apt install"
                DEPS="{gcc,g++,gcc-multilib,g++-multilib,ninja-build,python3-pip,python3-setuptools,python3-wheel,pkg-config,mesa-common-dev,libx11-dev,libxnvctrl-dev}"
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
            "openSUSE Leap"|"openSUSE Tumbleweed")
                MANAGER_QUERY="rpm -q"
                MANAGER_INSTALL="zypper install"
                DEPS="{gcc-c++,gcc-c++-32bit,meson,libpkgconf-devel,python3-Mako,libX11-devel,libX11-devel-32bit,glslang-devel,libglvnd-devel,libglvnd-devel-32bit,glibc-devel,glibc-devel-32bit,libstdc++-devel,libstdc++-devel-32bit,Mesa-libGL-devel,libXNVCtrl-devel}"
                install
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
        meson build/meson64 --libdir lib/mangohud/lib64 --prefix /usr -Dappend_libdir_mangohud=false ${CONFIGURE_OPTS}
    fi
    if [[ ! -f "build/meson32/build.ninja" ]]; then
        export CC="gcc -m32"
        export CXX="g++ -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH_32}"
        export LLVM_CONFIG="/usr/bin/llvm-config32"
        meson build/meson32 --libdir lib/mangohud/lib32 --prefix /usr -Dappend_libdir_mangohud=false ${CONFIGURE_OPTS}
    fi
}

build() {
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        configure
    fi
    DESTDIR="$PWD/build/release" ninja -C build/meson32 install
    DESTDIR="$PWD/build/release" ninja -C build/meson64 install
}

package() {
    LIB="build/release/usr/lib/mangohud/lib64/libMangoHud.so"
    LIB32="build/release/usr/lib/mangohud/lib32/libMangoHud.so"
    if [[ ! -f "$LIB" || "$LIB" -ot "build/meson64/src/libMangoHud.so" ]]; then
        build
    fi
    tar --numeric-owner --owner=0 --group=0 \
        -C build/release -cvf "build/MangoHud-package.tar" .
}

release() {
    rm build/MangoHud-package.tar
    mkdir -p build/MangoHud
    package
    cp --preserve=mode bin/mangohud-setup.sh build/MangoHud/mangohud-setup.sh
    cp build/MangoHud-package.tar build/MangoHud/MangoHud-package.tar
    tar --numeric-owner --owner=0 --group=0 \
        -C build -czvf build/MangoHud-$VERSION.tar.gz MangoHud
}

install() {
    rm -rf "$HOME/.local/share/MangoHud/"
    rm -f "$HOME/.local/share/vulkan/implicit_layer.d/"{mangohud32.json,mangohud64.json}

    [ "$UID" -eq 0 ] || mkdir -pv "${CONFIG_DIR}"
    [ "$UID" -eq 0 ] || exec sudo bash "$0" install

    /usr/bin/install -vm644 -D ./build/release/usr/lib/mangohud/lib32/libMangoHud.so /usr/lib/mangohud/lib32/libMangoHud.so
    /usr/bin/install -vm644 -D ./build/release/usr/lib/mangohud/lib64/libMangoHud.so /usr/lib/mangohud/lib64/libMangoHud.so
    /usr/bin/install -vm644 -D ./build/release/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86.json
    /usr/bin/install -vm644 -D ./build/release/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json
    /usr/bin/install -vm644 -D ./build/release/usr/share/doc/mangohud/MangoHud.conf.example /usr/share/doc/mangohud/MangoHud.conf.example

    /usr/bin/install -vm755 ./build/release/usr/bin/mangohud.x86 /usr/bin/mangohud.x86
    /usr/bin/install -vm755 ./build/release/usr/bin/mangohud /usr/bin/mangohud

    echo "MangoHud Installed"
}

clean() {
    rm -rf "build"
}

uninstall() {
    [ "$UID" -eq 0 ] || exec sudo bash "$0" uninstall
    rm -rfv "/usr/lib/mangohud"
    rm -rfv "/usr/share/doc/mangohud"
    rm -fv "/usr/share/vulkan/implicit_layer.d/mangohud.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json"
    rm -fv "/usr/bin/mangohud"
    rm -fv "/usr/bin/mangohud.x86"
}

usage() {
    if test -z $1; then
        echo "Unrecognized command argument: $a"
    else
        echo "$0 requires one argument"
    fi
    echo 'Accepted arguments: "pull", "configure", "build", "package", "install", "clean", "uninstall".'
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
        "release") release;;
        *)
            usage
    esac
done

if [[ -z $@ ]]; then
    usage no-args
fi

