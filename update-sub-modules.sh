#!/bin/zsh

if [ ! -d "third_party/elfuse" ]; then
  git submodule add git@github.com:open-sources-port/elfuse.git third_party/elfuse
  git commit -m "Add elfuse as submodule"
fi

echo "Updating submodules...."
# git submodule update --init --recursive
git submodule update --remote --recursive
