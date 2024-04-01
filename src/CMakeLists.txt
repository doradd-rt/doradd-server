cmake_minimum_required(VERSION 3.8)
project(dorad-server CXX)

include(ExternalProject)

add_executable(server main.cc)


# This is the correct way to link
#ExternalProject_Add(verona-rt
#  SOURCE_DIR ${CMAKE_SOURCE_DIR}/../external/verona-rt
#  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/verona-rt
#)
#add_dependencies(server verona-rt)
#target_link_libraries(server INTERFACE verona_rt)

target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/../external/verona-rt/src/rt)
# This shouldn't be a submodule
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/../external/snmalloc/src)

# enable external threading in verona-rt
target_compile_definitions(server PRIVATE -DVERONA_EXTERNAL_THREADING)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})
