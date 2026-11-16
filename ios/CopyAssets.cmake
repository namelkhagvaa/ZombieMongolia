# iOS: тоглоомд ачаалагддаг assets-ийг апп багцын assets/ руу хуулна (Windows-ийн суулгагчийнхтай ижил жагсаалт,
# tools/build_installer.ps1). Тоглоом компьютерийнх шиг <апп хавтас>/assets/-аас уншина (GetApplicationDirectory).
#   cmake -DSRC=<repo> -DDST=<App.app>/assets -P ios/CopyAssets.cmake
set(assets "${SRC}/assets")
set(rules
    ".|*_mesh.glb" ".|*_color.png" ".|window_icon.png" ".|home_back.png" ".|splash.png" ".|soldier_anims.bin"
    "icons|*.png" "huds|*.png" "terrain|*.png" "surfaces|*.jpg"
    "cars|*.vmesh" "cars|*_color.png" "buu|*.vmesh" "buu|*_color.png"
    "sounds|*.wav" "sounds/narration|*.wav" "trees|*.vmesh" "trees|*_color.png" "trees|*_far.bin"
    "pooshig|*.vmesh" "pooshig|*_color.png" "monuments|*.vmesh" "monuments|*_color.png"
    "broken_cars|*.vmesh" "broken_cars|*_color.png" "mission|*.vmesh" "mission|*_color.png" "city|*.bin")
set(count 0)
foreach(rule IN LISTS rules)
    string(REPLACE "|" ";" parts "${rule}")
    list(GET parts 0 folder)
    list(GET parts 1 pattern)
    file(GLOB files "${assets}/${folder}/${pattern}")
    list(FILTER files EXCLUDE REGEX "/man_[^/]*$") # (Mixamo-гийн эх дүр: тоглоомд ашиглагдахгүй)
    if(files)
        file(COPY ${files} DESTINATION "${DST}/${folder}")
        list(LENGTH files n)
        math(EXPR count "${count} + ${n}")
    endif()
endforeach()
# Үсгийн фонт: Android-тай ижил чөлөөт лицензтэй (src/UiFont.cpp)
file(COPY "${SRC}/android/ui_font.ttf" DESTINATION "${DST}")
message(STATUS "iOS: copied ${count} asset files to ${DST}")
