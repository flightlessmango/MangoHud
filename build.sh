#!/usr/bin/env bash
set -e

# Import the variables for dependencies
source ./build_deps.sh

OS_RELEASE_FILES=("/etc/os-release" "/usr/lib/os-release")
XDG_DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
XDG_CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
CONFIG_DIR="$XDG_CONFIG_HOME/MangoHud"
VERSION=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')
SU_CMD=$(command -v sudo || command -v doas || echo)
MACHINE=$(uname -m || echo)

# doas requires a double dash if the command it runs will include any dashes,
# so append a double dash to the command
[[ $SU_CMD == *doas ]] && SU_CMD="$SU_CMD -- "

# Correctly identify the os-release file.
for os_release in ${OS_RELEASE_FILES[@]} ; do
    if [[ ! -e "${os_release}" ]]; then
        continue
    fi
    DISTRO=$(sed -rn 's/^ID(_LIKE)*=(.+)/\L\2/p' ${os_release} | sed 's/"//g')
done

dependencies() {
    if [[ ! -f build/release/usr/lib/libMangoHud.so ]]; then
        missing_deps() {
            echo "# Missing dependencies:$INSTALL"
            read -rp "Do you wish the script to install these packages? [y/N]" PERMISSION
            case "$PERMISSION" in
                "y"|"Y") echo "Attempting to install missing packages"; sleep 0.5;;
                *) echo "Continuing with missing dependencies"; sleep 1;;
            esac
        }
        dep_install() {
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
                    $SU_CMD $MANAGER_INSTALL $INSTALL
                fi
            fi
            set -e
        }

        for i in $DISTRO; do
        echo "# Checking dependencies for \"$i\""
        case $i in
            *arch*|*manjaro*|*artix*|*SteamOS*)
                MANAGER_QUERY="pacman -Q"
                MANAGER_INSTALL="pacman -S"
                DEPS="{${DEPS_ARCH}}"
                dep_install
                break
            ;;
            *fedora*|*nobara*)
                MANAGER_QUERY="dnf list installed"
                MANAGER_INSTALL="dnf install"
                DEPS="{${DEPS_FEDORA}}"
                dep_install

                unset INSTALL
                DEPS="{glibc-devel.i686,libstdc++-devel.i686,libX11-devel.i686,wayland-devel.i686,libxkbcommon-devel.i686,python3-mako,meson,cmake,dbus-devel,glslang,libXNVCtrl-devel,libstdc++-static,gcc,gcc-c++,mesa-libGL-devel,python3-numpy,python3-matplotlib,libstdc++-static.i686
}"
                dep_install
                break
            ;;

            *debian*|*ubuntu*|*deepin*|*pop*)
                MANAGER_QUERY="dpkg-query -s"
                MANAGER_INSTALL="apt install"
                DEPS="{${DEPS_DEBIAN}}"
                dep_install

                if [[ $(pip3 show meson; echo $?) == 1 || $(pip3 show mako; echo $?) == 1 ]]; then
                    $SU_CMD pip3 install 'meson>=0.54' mako
                fi
                if [[ ! -f /usr/local/bin/glslangValidator ]]; then
                    wget https://github.com/KhronosGroup/glslang/releases/download/master-tot/glslang-master-linux-Release.zip
                    unzip glslang-master-linux-Release.zip bin/glslangValidator
                    $SU_CMD /usr/bin/install -m755 bin/glslangValidator /usr/local/bin/
                    rm bin/glslangValidator glslang-master-linux-Release.zip
                fi
                break
            ;;
            *suse*)
                echo "You may have to enable packman repository for some extra packages: ${DEPS_SUSE_EXTRA}"
                echo "Leap:       zypper ar -cfp 90 https://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Leap_15.1/ packman"
                echo "Tumbleweed: zypper ar -cfp 90 http://ftp.gwdg.de/pub/linux/misc/packman/suse/openSUSE_Tumbleweed/ packman"

                MANAGER_QUERY="rpm -q"
                MANAGER_INSTALL="zypper install"
                DEPS="{${DEPS_SUSE},${DEPS_SUSE_EXTRA}}"
                dep_install

                if [[ $(pip3 show meson; echo $?) == 1 ]]; then
                    $SU_CMD pip3 install 'meson>=0.54'
                fi
                break
            ;;
            *solus*)
                unset MANAGER_QUERY
                unset DEPS
                MANAGER_INSTALL="eopkg it"

                local packages=(${DEPS_SOLUS//,/ })

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
                dep_install
                break
                ;;
            *)
                echo "# Unable to find distro information!"
                echo "# Attempting to build regardless"
        esac
        done
    fi
}

configure() {
    dependencies
    git submodule update --init --depth 50
    CONFIGURE_OPTS="-Dwerror=true"
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        meson build/meson64 --libdir lib/mangohud/lib64 --prefix /usr -Dappend_libdir_mangohud=false $@ ${CONFIGURE_OPTS}
    fi
    if [[ ! -f "build/meson32/build.ninja" && "$MACHINE" = "x86_64" ]]; then
        export CC="gcc -m32"
        export CXX="g++ -m32"
        export PKG_CONFIG_PATH="/usr/lib32/pkgconfig:/usr/lib/i386-linux-gnu/pkgconfig:/usr/lib/pkgconfig:${PKG_CONFIG_PATH_32}"
        meson build/meson32 --libdir lib/mangohud/lib32 --prefix /usr -Dappend_libdir_mangohud=false $@ ${CONFIGURE_OPTS}
    fi
}

build() {
    if [[ ! -f "build/meson64/build.ninja" ]]; then
        configure $@
    fi
    DESTDIR="$PWD/build/release" ninja -C build/meson64 install

    if [ "$MACHINE" = "x86_64" ]; then
        DESTDIR="$PWD/build/release" ninja -C build/meson32 install
    fi

    sed -i 's:/usr/\\$LIB:/usr/lib/mangohud/\\$LIB:g' "$PWD/build/release/usr/bin/mangohud"
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

uninstall() {
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" uninstall
    rm -rfv "/usr/lib/mangohud"
    rm -rfv "/usr/share/doc/mangohud"
    rm -fv "/usr/share/man/man1/mangohud.1"
    rm -fv "/usr/share/vulkan/implicit_layer.d/mangohud.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json"
    rm -fv "/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json"
    rm -fv "/usr/bin/mangohud"
    rm -fv "/usr/bin/mangoplot"
    rm -fv "/usr/bin/mangohud.x86"
}

install() {
    rm -rf "$HOME/.local/share/MangoHud/"
    rm -f "$HOME/.local/share/vulkan/implicit_layer.d/"{mangohud32.json,mangohud64.json}

    [ "$UID" -eq 0 ] || mkdir -pv "${CONFIG_DIR}"
    [ "$UID" -eq 0 ] || build
    [ "$UID" -eq 0 ] || exec $SU_CMD bash "$0" install

    uninstall

    DEFAULTLIB=lib32
    for i in $DISTRO; do
        case $i in
            *arch*)
            DEFAULTLIB=lib64
            ;;
        esac
    done

    if [ "$MACHINE" != "x86_64" ]; then
        # Native libs
        DEFAULTLIB=lib64
    fi

    echo DEFAULTLIB: $DEFAULTLIB
    /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib64/libMangoHud.so /usr/lib/mangohud/lib64/libMangoHud.so
    /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib64/libMangoHud_dlsym.so /usr/lib/mangohud/lib64/libMangoHud_dlsym.so
    /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib64/libMangoHud_opengl.so /usr/lib/mangohud/lib64/libMangoHud_opengl.so
    if [ "$MACHINE" = "x86_64" ]; then
      /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib32/libMangoHud.so /usr/lib/mangohud/lib32/libMangoHud.so
      /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib32/libMangoHud_dlsym.so /usr/lib/mangohud/lib32/libMangoHud_dlsym.so
      /usr/bin/install -Dvm644 ./build/release/usr/lib/mangohud/lib32/libMangoHud_opengl.so /usr/lib/mangohud/lib32/libMangoHud_opengl.so
    fi

    /usr/bin/install -Dvm644 ./build/release/usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86_64.json
    /usr/bin/install -Dvm644 ./build/release/usr/share/vulkan/implicit_layer.d/MangoHud.x86.json /usr/share/vulkan/implicit_layer.d/MangoHud.x86.json
    /usr/bin/install -Dvm644 ./build/release/usr/share/man/man1/mangohud.1 /usr/share/man/man1/mangohud.1
    /usr/bin/install -Dvm644 ./build/release/usr/share/doc/mangohud/MangoHud.conf.example /usr/share/doc/mangohud/MangoHud.conf.example
    /usr/bin/install -vm755  ./build/release/usr/bin/mangohud /usr/bin/mangohud
    /usr/bin/install -vm755  ./build/release/usr/bin/mangoplot /usr/bin/mangoplot

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

    #ln -sv lib64 /usr/lib/mangohud/aarch64-linux-gnu
    #ln -sv lib64 /usr/lib/mangohud/arm-linux-gnueabihf

    echo "MangoHud Installed"
}

reinstall() {
    build
    package
    install
}

clean() {
    rm -rf "build"
    rm -rf subprojects/*/
}

usage() {
    if test -z $1; then
        echo "Unrecognized command argument: $arg"
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
    echo -e "\treinstall\tRuns build, then package, and finally install"
    echo -e "\tclean\t\tRemoves build directory"
    echo -e "\tuninstall\tRemoves installed MangoHud files from your system"
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
        "pull") git pull ${OPTS[@]};;
        "configure") configure ${OPTS[@]};;
        "build") build ${OPTS[@]};;
        "build_dbg") build --buildtype=debug -Dglibcxx_asserts=true ${OPTS[@]};;
        "package") package;;
        "install") install;;
        "reinstall") reinstall;;
        "clean") clean;;
        "uninstall") uninstall;;
        "release") release;;
        *)
            usage
    esac
done
