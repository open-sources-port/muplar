#!/bin/zsh

if [ ! -d "third_party/elfuse" ]; then
  git submodule add git@github.com:open-sources-port/elfuse.git third_party/elfuse
  git commit -m "Add elfuse as submodule"
fi

if [ ! -d "third_party/angle" ]; then
  git submodule add https://github.com/google/angle.git third_party/angle
  git commit -m "Add angle as submodule"
fi

echo "Updating submodules...."
git submodule update --init --recursive
