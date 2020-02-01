#!/bin/bash    

DATA_DIR=$HOME/.local/share/MangoHud
LAYER=build/release/usr/share/vulkan/implicit_layer.d/mangohud.json
IMPLICIT_LAYER_DIR=$HOME/.local/share/vulkan/implicit_layer.d 

dependencies() {
    if [[ ! -f build/release/usr/lib64/libMangoHud.so ]]; then
        DISTRO=$(sed 1q /etc/os-release | sed 's/NAME=//g' | sed 's/"//g')
        echo "# Checking Dependencies"

        missing_deps() {
            echo "# Missing dependencies!"
            echo "# Attempting to install '$INSTALL'"
        }
        case $DISTRO in
            "Arch Linux"|"Manjaro")
                for i in {gcc,meson,pkgconf,python-mako,glslang,libglvnd,lib32-libglvnd}; do
                    pacman -Q $i &> /dev/null
                    if [[ $? == 1 ]]; then
                        INSTALL=$INSTALL" "$i
                    fi
                done
                if [[ ! -z "$INSTALL" ]]; then
                    missing_deps
                    sudo pacman -S $INSTALL
                fi
                ;;
            "Fedora")
                for i in {meson,gcc,g++,libX11-devel,glslang,python-mako,mesa-libGL-devel,glibc-devel.i686,libstdc++-devel.i686}; do
                    dnf list installed | grep $i &> /dev/null
                    if [[ $? == 1 ]]; then
                        INSTALL=$INSTALL" "$i
                    fi
                done
                if [[ ! -z "$INSTALL" ]]; then
                    missing_deps
                    sudo dnf install $INSTALL
                fi
                ;;
            "Ubuntu"|"Linux Mint"|"Debian")
                for i in {gcc,g++,gcc-multilib,g++-multilib,ninja-build,python3-pip,python3-setuptools,python3-wheel,pkg-config,mesa-common-dev,libx11-dev:i386}; do
                    dpkg-query -l $i &> /dev/null
                    if [[ $? == 1 ]]; then
                        INSTALL=$INSTALL" "$i
                    fi
                done
                if [[ ! -z "$INSTALL" ]]; then
                    missing_deps
                    sudo apt install $INSTALL
                    sudo pip3 install meson mako
                fi
                if [[ ! -f /usr/local/bin/glslangValidator ]]; then
                    wget https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
                    unzip glslang-master-linux-Release.zip bin/glslangValidator
                    sudo install -m755 bin/glslangValidator /usr/local/bin/
                    rm bin/glslangValidator glslang-master-linux-Release.zip
                fi
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
    if [[ ! -d build/meson64 ]]; then
        meson build/meson64 --libdir lib64 --prefix $PWD/build/release/usr

        export CC="gcc -m32"
        export CXX="g++ -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:${PKG_CONFIG_PATH_32}"
        export LLVM_CONFIG="/usr/bin/llvm-config32"
        meson build/meson32 --libdir lib32 --prefix $PWD/build/release/usr
    fi
}

build() {
    ninja -C build/meson32 install
    ninja -C build/meson64 install
}

install() {
    mkdir -p $IMPLICIT_LAYER_DIR
    mkdir -p $DATA_DIR

    cp build/release/usr/lib32/libMangoHud.so $DATA_DIR/libMangoHud32.so
    cp build/release/usr/lib64/libMangoHud.so $DATA_DIR/libMangoHud.so
    cp $LAYER $IMPLICIT_LAYER_DIR/mangohud64.json
    cp $LAYER $IMPLICIT_LAYER_DIR/mangohud32.json

    sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud32.so|g" $IMPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud.so|g" $IMPLICIT_LAYER_DIR/mangohud64.json
    sed -i "s|64bit|32bit|g" $IMPLICIT_LAYER_DIR/mangohud32.json
}

package() {
    VERSION=$(printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)")
    cd build/release
    tar czf ../../MangoHud-$VERSION.tar.gz *
}

clean() {
    rm -rf build
}

uninstall() {
    rm -r $HOME/.local/share/MangoHud
    rm $IMPLICIT_LAYER_DIR/{mangohud64,mangohud32}.json
}

case $1 in
    "") configure; build;;
    "build") configure; build;;
    "install") configure; build; install;;
    "update") git pull &> /dev/null; configure; build; install;;
    "package") package;;
    "clean") clean;;
    "uninstall") uninstall;;
    *)
        echo "Unrecognized command argument: $1"
        echo 'Accepted arguments: "", "build", "install", "update", "package", "clean", "uninstall".'
esac
