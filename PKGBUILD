pkgname=('mangohud' 'lib32-mangohud')
pkgver=r24.b67a2aa
pkgrel=1
pkgdesc="Vulkan overlay layer to display information about the application"
arch=('x86_64')
makedepends=('gcc' 'meson' 'python-mako' 'libx11' 'lib32-libx11' 'git')
depends=('glslang' 'libglvnd' 'lib32-libglvnd' 'vulkan-headers')
replaces=('vulkan-mesa-layer-mango' 'lib32-vulkan-mesa-layer-mango')
url="https://github.com/flightlessmango/MangoHud"

pkgver() {
    cd $startdir
    printf "r%s.%s" "$(git rev-list --count HEAD)" "$(git rev-parse --short HEAD)"
}

prepare() {
  git submodule update --init --depth 50
}

build() {
    cd $startdir
    # ./build.sh clean
    ./build.sh build
}

package_mangohud() {
  provides=("mangohud=${pkgver}")
  cd $startdir
  install -Dm664 "build/release/usr/lib64/libMangoHud.so" "${pkgdir}/usr/lib/libMangoHud.so"
  install -Dm664 "build/release/usr/share/vulkan/implicit_layer.d/mangohud.json" "${pkgdir}/usr/share/vulkan/implicit_layer.d/mangohud.json"
}
package_lib32-mangohud() {
  provides=("lib32-mangohud=${pkgver}")
  cd $startdir
  install -Dm664 "build/release/usr/lib32/libMangoHud.so" "${pkgdir}/usr/lib32/libMangoHud.so"
}
