#!/bin/zsh

zip -r ../muplar.zip *.sh CMakeLists.txt LICENSE README.md cli linker64 mup.entitlements \
  muplar_roadmap_to_android_app.svg platform rependencies runtime \
  tests third_party tools .gitmodules .gitignore

zip -r ../muplar-no-thridparty.zip *.sh CMakeLists.txt LICENSE README.md cli linker64 mup.entitlements \
  muplar_roadmap_to_android_app.svg platform rependencies runtime \
  tests tools .gitmodules .gitignore
