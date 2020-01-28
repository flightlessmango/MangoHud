#!/bin/bash    

DATA_DIR=$HOME/.local/share/MangoHud
LAYER=build/release/share/vulkan/implicit_layer.d/mangohud.json
IMPLICIT_LAYER_DIR=$HOME/.local/share/vulkan/implicit_layer.d 

configure() {
    if [[ ! -d build/meson64 ]]; then
        meson build/meson64 --libdir lib64 --prefix $PWD/build/release

        export CC="gcc -m32"
        export CXX="g++ -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig"
        export LLVM_CONFIG="/usr/bin/llvm-config32"
        meson build/meson32 --libdir lib32 --prefix $PWD/build/release
    fi
}

build() {
    ninja -C build/meson32 install
    ninja -C build/meson64 install
}

install() {
    mkdir -p $IMPLICIT_LAYER_DIR
    mkdir -p $DATA_DIR

    cp build/release/lib32/libMangoHud.so $DATA_DIR/libMangoHud32.so
    cp build/release/lib64/libMangoHud.so $DATA_DIR/libMangoHud.so
    cp $LAYER $IMPLICIT_LAYER_DIR/mangohud64.json
    cp $LAYER $IMPLICIT_LAYER_DIR/mangohud32.json

    sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud32.so|g" $IMPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|libMangoHud.so|$HOME/.local/share/MangoHud/libMangoHud.so|g" $IMPLICIT_LAYER_DIR/mangohud64.json
    sed -i "s|64bit|32bit|g" $IMPLICIT_LAYER_DIR/mangohud32.json
}

clean() {
    rm -r build
}

uninstall() {
    rm -r $HOME/.local/share/MangoHud
    rm $IMPLICIT_LAYER_DIR/{mangohud64,mangohud32}.json
}

case $1 in
    "") configure; build;;
    "build") configure; build;;
    "install") configure; build; install;;
    "clean") clean;;
    "uninstall") uninstall;;
    *)
        echo "Unrecognized command argument: $1"
        echo 'Accepted arguments: "", "build", "install", "package", "clean", "uninstall".'
esac
