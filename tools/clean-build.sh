#!/bin/zsh

echo "Cleanning up [build] folder..."
find build -mindepth 1 -maxdepth 1 \
  ! -name wine-build \
  ! -name wine-prefix \
  ! -name dxmt-build \
  -exec rm -rfv {} +

echo "Cleanning up [third_party/elfuse/build] folder..."
rm -rfv third_party/elfuse/build

echo "Done"
