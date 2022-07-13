#!/usr/bin/env bash
# Specialized build script for Steam Runtime SDK docker
set -e

IFS=" " read -ra debian_chroot < /etc/debian_chroot
LOCAL_CC=${CC:-gcc-5}
LOCAL_CXX=${CXX:-g++-5}
RUNTIME=${RUNTIME:-${debian_chroot[1]}}
SRT_VERSION=${SRT_VERSION:-${debian_chroot[2]}}
VERSION=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')

dependencies() {

    if [[ ! -f build-srt/release/usr/lib/libMangoHud.so ]]; then
        install() {
            set +e
            for i in ${DEPS[@]}; do
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
        DEPS=(${LOCAL_CC}-multilib ${LOCAL_CXX}-multilib unzip)
        install


        # use py3.5 with scout, otherwise hope python is new enough
        set +e
        which python3.5 >/dev/null
        if [ $? -eq 0 ]; then
            # py3.2 is weird
            ln -sf python3.5 /usr/bin/python3
        fi
        set -e

        if [[ ! -f ./bin/get-pip.py ]]; then
            curl https://bootstrap.pypa.io/pip/3.5/get-pip.py -o bin/get-pip.py
            python3 ./bin/get-pip.py
        fi
        pip3 install 'meson>=0.54' mako

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
    if [[ ! -f "build-srt/meson64/build.ninja" ]]; then
        export CC="${LOCAL_CC}"
        export CXX="${LOCAL_CXX}"
        meson build-srt/meson64 --libdir lib/mangohud/lib --prefix /usr -Dappend_libdir_mangohud=false -Dld_libdir_prefix=true $@ ${CONFIGURE_OPTS}
    fi
    if [[ ! -f "build-srt/meson32/build.ninja" ]]; then
        export CC="${LOCAL_CC} -m32"
        export CXX="${LOCAL_CXX} -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH_32}"
        meson build-srt/meson32 --libdir lib/mangohud/lib32 --prefix /usr -Dappend_libdir_mangohud=false -Dld_libdir_prefix=true $@ ${CONFIGURE_OPTS}
    fi
}

build() {
    if [[ ! -f "build-srt/meson64/build.ninja" || ! -f "build-srt/meson32/build.ninja" ]]; then
        configure $@
    fi
    DESTDIR="$PWD/build-srt/release" ninja -C build-srt/meson32 install
    DESTDIR="$PWD/build-srt/release" ninja -C build-srt/meson64 install
}

package() {
    LIB="build-srt/release/usr/lib/mangohud/lib/libMangoHud.so"
    LIB32="build-srt/release/usr/lib/mangohud/lib32/libMangoHud.so"
    if [[ ! -f "$LIB" || "$LIB" -ot "build-srt/meson64/src/libMangoHud.so" ]]; then
        build
    fi
    tar --numeric-owner --owner=0 --group=0 \
        -C build-srt/release -cvf "build-srt/MangoHud-package.tar" .
}

release() {
    rm build-srt/MangoHud-package.tar
    mkdir -p build-srt/MangoHud
    package
    cp --preserve=mode bin/mangohud-setup.sh build-srt/MangoHud/mangohud-setup.sh
    cp build-srt/MangoHud-package.tar build-srt/MangoHud/MangoHud-package.tar
    tar --numeric-owner --owner=0 --group=0 \
        -C build-srt -czvf build-srt/MangoHud-${VERSION}_${RUNTIME}-${SRT_VERSION}.tar.gz MangoHud
}

clean() {
    rm -rf "build-srt/"
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
    echo -e "\tclean\t\tRemoves build directory"
    echo -e "\trelease\t\tBuilds a MangoHud release tar package"
}

if [[ -z $@ ]]; then
    usage no-args
fi

while [ $# -gt 0 ]; do
    OPTS=()
    arg="$1"
    shift

    while [ $# -gt 0 ] ; do
        case $1 in
        -*)
            OPTS+=("$1")
            shift
        ;;
        *)
            break
        ;;
        esac;
    done

    echo -e "\e[1mCommand:\e[92m" $arg "\e[94m"${OPTS[@]}"\e[39m\e[0m"
    case $arg in
        "configure") configure ${OPTS[@]};;
        "build") build ${OPTS[@]};;
        "package") package;;
        "clean") clean;;
        "release") release;;
        *)
            usage
    esac
done
