cmake_minimum_required(VERSION 3.20)
project(cnc_clone CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable SDL2 lives in tools/SDL2 (VC development package).
set(SDL2_DIR "${CMAKE_SOURCE_DIR}/tools/SDL2/cmake" CACHE PATH "SDL2 cmake config dir")
find_package(SDL2 REQUIRED)

add_library(formats STATIC
    src/formats/aud.cpp
    src/formats/cps.cpp
    src/formats/ini.cpp
    src/formats/lcw.cpp
    src/formats/palette.cpp
    src/formats/shp.cpp
    src/formats/shpd2.cpp
    src/formats/tmp.cpp
)
target_include_directories(formats PUBLIC src)

add_library(game STATIC
    src/game/house.cpp
    src/game/map.cpp
    src/game/render.cpp
    src/game/rules.cpp
    src/game/sim.cpp
)
target_link_libraries(game PUBLIC formats SDL2::SDL2)

# The playable shell (target name game_exe: "game" is taken by the library).
add_executable(game_exe src/game_main.cpp)
set_target_properties(game_exe PROPERTIES OUTPUT_NAME game)
target_link_libraries(game_exe PRIVATE game SDL2::SDL2 SDL2::SDL2main)

add_executable(shpview src/tools/shpview.cpp)
target_link_libraries(shpview PRIVATE formats SDL2::SDL2 SDL2::SDL2main)

add_executable(tmpview src/tools/tmpview.cpp)
target_link_libraries(tmpview PRIVATE formats SDL2::SDL2 SDL2::SDL2main)

add_executable(iniquery src/tools/iniquery.cpp)
target_link_libraries(iniquery PRIVATE formats)

add_executable(auddump src/tools/auddump.cpp)
target_link_libraries(auddump PRIVATE formats)

add_executable(mapview src/tools/mapview.cpp)
target_link_libraries(mapview PRIVATE game SDL2::SDL2 SDL2::SDL2main)

# SDL2.dll next to the executables
add_custom_command(TARGET shpview POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:shpview>)
