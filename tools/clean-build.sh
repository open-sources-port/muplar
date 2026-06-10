#!/bin/zsh

echo "Cleanning up [build] folder..."
find build -mindepth 1 -maxdepth 1 \
  ! -name wine-build \
  ! -name wine-prefix \
  ! -name dxmt-build \
  ! -name busybox-x86_64 \
  ! -name busybox-aarch64 \
  ! -name busybox_external-prefix \
  ! -name wawona_external-prefix \
  ! -name bin \
  -exec rm -rfv {} +

echo "Cleanning up [third_party/elfuse/build] folder..."
rm -rfv third_party/elfuse/build

echo "Done"
