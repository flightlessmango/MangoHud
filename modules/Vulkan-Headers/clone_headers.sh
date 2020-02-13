#!/bin/sh

GIT="$1"
BUILD_DIR="$2"

if [ ! -f "$BUILD_DIR/registry/vk.xml" ]; then
  "$GIT" clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers.git "$BUILD_DIR"
fi

ln -sf "registry/vk.xml" modules/Vulkan-Headers/
ln -sf "include/vulkan" modules/Vulkan-Headers/
