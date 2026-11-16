#!/bin/sh
# Xcode-ийн build-ийн дараа (CMakeLists.txt: POST_BUILD): assets-ийг апп багц руу хуулна. Апп багцын жинхэнэ замыг
# Xcode-ийн орчны хувьсагчаас авна: App Store-д зориулсан archive үед апп өөр хавтсанд build хийгдэнэ.
#   sh ios/copy_assets.sh <repo> <cmake>
set -e
exec "$2" -DSRC="$1" -DDST="${TARGET_BUILD_DIR}/${WRAPPER_NAME}/assets" -P "$1/ios/CopyAssets.cmake"
