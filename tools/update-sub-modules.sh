#!/bin/zsh

if [ ! -d "third_party/elfuse" ]; then
  git submodule add git@github.com:open-sources-port/elfuse.git third_party/elfuse
  git commit -m "Add elfuse as submodule"
fi

if [ ! -d "third_party/wine" ]; then
  git submodule add --depth 1 https://gitlab.winehq.org/wine/wine.git third_party/wine
  git commit -m "Add wine as submodule"
fi

if [ ! -d "third_party/dxmt" ]; then
  git submodule add --depth 1 https://github.com/3Shain/dxmt.git third_party/dxmt
  git commit -m "Add dxmt as submodule"
fi

echo "Updating submodules...."
git submodule update --init --recursive
# git submodule update --remote --recursive
