#!/usr/bin/env bash
set -e

# Specialized build script for Steam Runtime SDK docker

OS_RELEASE_FILES=("/etc/os-release" "/usr/lib/os-release")
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_DIR="$XDG_DATA_HOME/MangoHud"
CONFIG_DIR="$XDG_CONFIG_HOME/MangoHud"
LAYER="build/release/usr/share/vulkan/implicit_layer.d/mangohud.json"
INSTALL_DIR="build/package/"
IMPLICIT_LAYER_DIR="$XDG_DATA_HOME/vulkan/implicit_layer.d"
VERSION=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')

dependencies() {

    if [[ ! -f build/release/usr/lib64/libMangoHud.so ]]; then
        install() {
            set +e
            for i in $(eval echo $DEPS); do
                dpkg-query -s "$i" &> /dev/null
                if [[ $? == 1 ]]; then
                    INSTALL="$INSTALL""$i "
                fi
            done
            if [[ ! -z "$INSTALL" ]]; then
                apt-get update
                apt-get -y install $INSTALL
            fi
            set -e
        }

        echo "# Checking Dependencies"
        DEPS="{gcc-5-multilib,g++-5-multilib,unzip}"
        install

        # py3.2 is weird
        ln -sf python3.5 /usr/bin/python3

        if [[ ! -f ./bin/get-pip.py ]]; then
            curl https://bootstrap.pypa.io/get-pip.py -o bin/get-pip.py
            python3.5 ./bin/get-pip.py
        fi

        if [[ $(pip3.5 show meson; echo $?) == 1 || $(pip3.5 show mako; echo $?) == 1 ]]; then
            pip3.5 install meson mako
        fi

        if [[ ! -f /usr/include/NVCtrl/NVCtrl.h ]]; then
            curl -LO http://mirrors.kernel.org/ubuntu/pool/main/n/nvidia-settings/libxnvctrl0_440.64-0ubuntu1_amd64.deb
            curl -LO http://mirrors.kernel.org/ubuntu/pool/main/n/nvidia-settings/libxnvctrl-dev_440.64-0ubuntu1_amd64.deb
            dpkg -i libxnvctrl0_440.64-0ubuntu1_amd64.deb libxnvctrl-dev_440.64-0ubuntu1_amd64.deb
        fi

        # preinstalled 7.10.xxxx
        #if [[ ! -f /usr/local/bin/glslangValidator ]]; then
        #    curl -LO https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
        #    unzip glslang-master-linux-Release.zip bin/glslangValidator
        #    /usr/bin/install -m755 bin/glslangValidator /usr/local/bin/
        #    rm bin/glslangValidator glslang-master-linux-Release.zip
        #fi
    fi
}

configure() {
    dependencies
    git submodule update --init
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        export CC="gcc-5"
        export CXX="g++-5"
        meson build/meson64 --libdir lib/mangohud/lib64 --prefix /usr -Dappend_libdir_mangohud=false ${CONFIGURE_OPTS}
    fi
    if [[ ! -f "build/meson32/build.ninja" ]]; then
        export CC="gcc-5 -m32"
        export CXX="g++-5 -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH_32}"
        export LLVM_CONFIG="/usr/bin/llvm-config32"
        meson build/meson32 --libdir lib/mangohud/lib32 --prefix /usr -Dappend_libdir_mangohud=false ${CONFIGURE_OPTS}
    fi
}

build() {
    if [[ ! -f "build/meson64/build.ninja" || ! -f "build/meson32/build.ninja" ]]; then
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
    /usr/bin/install -vm644 -D ./build/release/usr/lib/mangohud/lib32/libMangoHud_dlsym.so /usr/lib/mangohud/lib32/libMangoHud_dlsym.so
    /usr/bin/install -vm644 -D ./build/release/usr/lib/mangohud/lib64/libMangoHud_dlsym.so /usr/lib/mangohud/lib64/libMangoHud_dlsym.so
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
    echo -e "\nUsage: $0 <command>\n"
    echo "Available commands:"
    echo -e "\tpull\t\tPull latest commits (code) from Git"
    echo -e "\tconfigure\tEnsures that dependencies are installed, updates git submodules, and generates files needed for building MangoHud. This is automatically run by the build command"
    echo -e "\tbuild\t\tIf needed runs configure and then builds (compiles) MangoHud"
    echo -e "\tpackage\t\tRuns build if needed and then builds a tar package from MangoHud"
    echo -e "\tinstall\t\tInstall MangoHud onto your system"
    echo -e "\tclean\t\tRemoves build directory"
    echo -e "\tuninstall\tRemoves installed MangoHud files from your system"
    echo -e "\trelease\t\tBuilds a MangoHud release tar package"
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

