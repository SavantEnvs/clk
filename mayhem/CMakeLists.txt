cmake_minimum_required(VERSION 3.19)
project(clk_mayhem LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(CLK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
include("${CLK_ROOT}/cmake/CLK_SOURCES.cmake")

# The media layer: everything the static analyser and the disk/tape/cartridge/mass-storage/snapshot
# formats need, without the machines, the OpenGL output or the SDL front end.
set(CLK_MEDIA_SOURCES ${CLK_SOURCES})
list(FILTER CLK_MEDIA_SOURCES INCLUDE REGEX "^(Analyser/Static|Storage|Components/(1770|8272|DiskII)|InstructionSets|Processors|Reflection)/")
list(FILTER CLK_MEDIA_SOURCES EXCLUDE REGEX "^Analyser/Dynamic")
list(TRANSFORM CLK_MEDIA_SOURCES PREPEND "${CLK_ROOT}/")

add_library(clk_media STATIC ${CLK_MEDIA_SOURCES})
target_include_directories(clk_media PUBLIC "${CLK_ROOT}")
find_package(ZLIB REQUIRED)
target_link_libraries(clk_media PUBLIC ZLIB::ZLIB)
