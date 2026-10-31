# ProJ IDE — CMake (server variant by default; FLTK variant optional)
cmake_minimum_required(VERSION 3.16)
project(ProjIDE CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

add_executable(projide
    src/util.cpp src/lang.cpp src/build.cpp src/store.cpp src/install.cpp
    src/http.cpp src/jsonlite.cpp src/api.cpp src/server_main.cpp)
target_link_libraries(projide PRIVATE Threads::Threads)

find_package(FLTK QUIET)
if(FLTK_FOUND)
    add_executable(projide-fltk
        src/util.cpp src/lang.cpp src/build.cpp src/store.cpp src/install.cpp
        src_fltk/theme.cpp src_fltk/highlight.cpp src_fltk/main.cpp)
    target_link_libraries(projide-fltk PRIVATE ${FLTK_LIBRARIES} Threads::Threads)
    target_include_directories(projide-fltk PRIVATE ${FLTK_INCLUDE_DIRS})
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(WEBKIT QUIET webkit2gtk-4.1)
    if(WEBKIT_FOUND)
        file(GLOB ENGINE_SRC CONFIGURE_DEPENDS src/*.cpp)
        list(FILTER ENGINE_SRC EXCLUDE REGEX "server_main")
        add_executable(projide-app ${ENGINE_SRC} src_shell/shell_main.cpp)
        target_include_directories(projide-app PRIVATE ${WEBKIT_INCLUDE_DIRS} src)
        target_link_libraries(projide-app PRIVATE ${WEBKIT_LIBRARIES} Threads::Threads)
    endif()
endif()

if(WIN32)
    target_link_libraries(projide PRIVATE ws2_32)
endif()
