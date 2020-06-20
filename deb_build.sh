#!/usr/bin/env bash

name="MangoHud"
version=$(git describe --long --tags --always | sed 's/\([^-]*-g\)/r\1/;s/-/./g;s/^v//')
maintainer="flightlessmango"
vendor="flightlessmango"
url="https://github.com/flightlessmango/MangoHud"
description="A Vulkan and OpenGL overlay for monitoring FPS, temperatures, CPU/GPU load and more. "
dependencies=""
architecture="amd64"
provides="MangoHud"
files="build/release/=/"

rb_version="2.6.3"
rb_gem=""
rb_fpm=""

install_gem() {
    sudo apt install -y gnupg2 curl libaugeas-dev
    gpg2 --recv-keys 409B6B1796C275462A1703113804BB82D39DC0E3 7D2BAF1CF37B13E2069D6956105BD0E739499BDB
    curl -sSL https://get.rvm.io | bash -s stable
    source $HOME/.rvm/scripts/rvm
    rvm install $rb_version
    rvm use 2.6.3
}

install_fpm() {
    gem install fpm --no-document
}

detect_gem() {
    if [[ ! -f $(which gem) ]]; then
        echo "gem not found"
        install_gem
    else
        echo "gem found installed: " $(which gem)
    fi
    rb_gem=$(which gem)
}

detect_fpm() {
    if [[ ! -f $(which fpm) ]]; then
        echo "fpm not found"
        install_fpm
    else
        echo "fpm found installed: " $(which fpm)
    fi
    rb_fpm=$(which fpm)
}

do_deb() {
    rm -rf *.deb
    ${rb_fpm} -s dir \
        -t deb \
        -f \
        -n "${name}" \
        -v "${version}" \
        -d "${dependencies}" \
        -a "${architecture}" \
        -m "${maintainer}" \
        --deb-no-default-config-files \
        --url "${url}" \
        --description "${description}" \
        --vendor "${vendor}" \
        --provides "${provides}" \
        ${files}
}

detect_gem
detect_fpm

/bin/bash build.sh build

do_deb
