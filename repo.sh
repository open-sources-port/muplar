#!/bin/zsh

if [ ! -d "third_party/elfuse" ]; then
  git subtree add --prefix=third_party/elfuse git@github.com:sysprog21/elfuse.git main --squash
else
  git subtree pull --prefix=third_party/elfuse git@github.com:sysprog21/elfuse.git main --squash
fi
