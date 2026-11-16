# iOS: raylib-д iOS-ийн платформ байхгүй тул SDL2-ийн давхаргаар (PLATFORM_DESKTOP_SDL) хөрвүүлнэ: SDL2 нь iOS-ийн
# цонх (UIKit), OpenGL ES 3.0, хүрэлт, гар, дууг гүйцэтгэнэ. raylib-ийн CMake нь SDL горимд SDL2-ийг системээс хайдаг
# тул raylib-ийн эх файлуудаас өөрөө сан угсарна. CMakeLists.txt-ээс include хийнэ (raylib_SOURCE_DIR тохируулна).
enable_language(OBJC OBJCXX)
# App Store-ийн archive-д сангууд (SDL2, raylib, ENet) суулгагдахгүй: эс бөгөөс Xcode "Generic Archive" үүсгэж .ipa гарахгүй
set(CMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL "YES")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.32.10
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 5.5
    SOURCE_SUBDIR ios-no-cmake # (raylib-ийн өөрийн CMake-ийг ажиллуулахгүй: эх файлууд л)
)
FetchContent_MakeAvailable(raylib)

set(RAYLIB_SRC ${raylib_SOURCE_DIR}/src)
add_library(raylib STATIC
    ${RAYLIB_SRC}/rcore.c
    ${RAYLIB_SRC}/rshapes.c
    ${RAYLIB_SRC}/rtextures.c
    ${RAYLIB_SRC}/rtext.c
    ${RAYLIB_SRC}/rmodels.c
    ${RAYLIB_SRC}/raudio.c
    ${RAYLIB_SRC}/utils.c
)
# miniaudio iOS дээр AVAudioSession-ийг тохируулахын тулд Objective-C-ээр хөрвөх ёстой
set_source_files_properties(${RAYLIB_SRC}/raudio.c PROPERTIES LANGUAGE OBJC)
target_compile_definitions(raylib
    PUBLIC PLATFORM_DESKTOP_SDL GRAPHICS_API_OPENGL_ES3
    PRIVATE SUPPORT_FILEFORMAT_JPG=1 GLES_SILENCE_DEPRECATION GL_SILENCE_DEPRECATION)
# ios/include: <GLES3/gl3.h> -> OpenGLES.framework (rlgl.h Khronos-ийн нэрээр дууддаг)
target_include_directories(raylib PUBLIC ${RAYLIB_SRC} ${CMAKE_SOURCE_DIR}/ios/include)
target_link_libraries(raylib PUBLIC SDL2::SDL2-static "-framework OpenGLES" "-framework AVFoundation"
                      "-framework AudioToolbox" "-framework CoreAudio")
