# Build бүрд: тоглоомын харуулах хувилбарын цагийг (минутаар) OUT файлд бичнэ. Агуулга өөрчлөгдөөгүй бол файлд
# хүрэхгүй (src/BuildStamp.cpp дахин хөрвүүлэгдэхгүй). Ажиллуулах: cmake -DOUT=<зам> -P BuildStamp.cmake
string(TIMESTAMP stamp "%Y-%m-%d %H:%M")
set(content "#pragma once\n#define ZM_BUILD_STAMP \"${stamp}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" previous)
    if(previous STREQUAL content)
        return()
    endif()
endif()
file(WRITE "${OUT}" "${content}")
