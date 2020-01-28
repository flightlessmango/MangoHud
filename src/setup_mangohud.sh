#!/bin/bash
MANGOHUD_DIR=$HOME/.local/share/MangoHud/
LIB64=$HOME/.local/share/MangoHud/libMangoHud64.so
LIB32=$HOME/.local/share/MangoHud/libMangoHud32.so
IMPLICIT_LAYER_DIR=$HOME/.local/share/vulkan/implicit_layer.d 
EXPLICIT_LAYER_DIR=$HOME/.local/share/vulkan/explicit_layer.d

install() {
    mkdir -p $IMPLICIT_LAYER_DIR
    mkdir -p $EXPLICIT_LAYER_DIR
    mkdir -p $MANGOHUD_DIR
    cp -v x32/libMangoHud32.so $MANGOHUD_DIR     
    cp -v x64/libMangoHud64.so $MANGOHUD_DIR
    cp -v implicit_layer.d/mangohud32.json $IMPLICIT_LAYER_DIR
    cp -v implicit_layer.d/mangohud64.json $IMPLICIT_LAYER_DIR
    cp -v explicit_layer.d/mangohud32.json $EXPLICIT_LAYER_DIR
    cp -v explicit_layer.d/mangohud64.json $EXPLICIT_LAYER_DIR
    sed -i "s|libMangoHud.so|$LIB32|g" $IMPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|libMangoHud.so|$LIB64|g" $IMPLICIT_LAYER_DIR/mangohud64.json
    sed -i "s|64bit|32bit|g" $IMPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|libMangoHud.so|$LIB32|g" $EXPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|libMangoHud.so|$LIB64|g" $EXPLICIT_LAYER_DIR/mangohud64.json
    sed -i "s|64bit|32bit|g" $EXPLICIT_LAYER_DIR/mangohud32.json
    sed -i "s|mangohud|mangohud32|g" $EXPLICIT_LAYER_DIR/mangohud32.json
}

uninstall() {
    rm -v $MANGOHUD_DIR/libMangoHud32.so
    rm -v $MANGOHUD_DIR/libMangoHud64.so
    rm -v $IMPLICIT_LAYER_DIR/mangohud32.json
    rm -v $IMPLICIT_LAYER_DIR/mangohud64.json
}

case $1 in
    "install")
        install
    ;;
    "uninstall")
        uninstall
    ;;
    *)
        echo "Unrecognized action: $1"
        echo "Usage: $0 [install|uninstall]"
        exit 1
    ;;
esac