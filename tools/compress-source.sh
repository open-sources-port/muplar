#!/bin/zsh

export OUTPUT_ZIP=../muplar.zip
COMMON_FILES=(
  CMakeLists.txt
  LICENSE
  README.md
  cli
  linker64
  mup.entitlements
  platform
  rependencies
  docs
  tests
  tools
  .gitmodules
  .gitignore
  apps
)

if [ -f "${OUTPUT_ZIP}" ]; then
  echo "Removing existing ${OUTPUT_ZIP}"
  rm "${OUTPUT_ZIP}"
fi
zip -r "${OUTPUT_ZIP}" "${COMMON_FILES[@]}" third_party/elfuse/src

export OUTPUT_NO_THIRDPARTY_ZIP=../muplar-no-thirdparty.zip
if [ -f "${OUTPUT_NO_THIRDPARTY_ZIP}" ]; then
  echo "Removing existing ${OUTPUT_NO_THIRDPARTY_ZIP}"
  rm "${OUTPUT_NO_THIRDPARTY_ZIP}"
fi
zip -r "${OUTPUT_NO_THIRDPARTY_ZIP}" "${COMMON_FILES[@]}"
