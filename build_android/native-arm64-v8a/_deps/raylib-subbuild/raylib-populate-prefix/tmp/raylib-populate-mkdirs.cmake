# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-src")
  file(MAKE_DIRECTORY "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-src")
endif()
file(MAKE_DIRECTORY
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-build"
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix"
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/tmp"
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp"
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/src"
  "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/ZombieMongolia/build_android/native-arm64-v8a/_deps/raylib-subbuild/raylib-populate-prefix/src/raylib-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
