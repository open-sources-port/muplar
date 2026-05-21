#!/bin/zsh

if [ ! -d "third_party/elfuse" ]; then
  git submodule add git@github.com:open-sources-port/elfuse.git third_party/elfuse
  git commit -m "Add elfuse as submodule"
else
  echo "Updating submodules...."
  git submodule update --init --recursive
fi
