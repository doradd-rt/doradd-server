cmake_minimum_required(VERSION 3.8)
project(dorad-server CXX)

include(ExternalProject)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(-Wno-pointer-arith)
add_executable(server main.cc net.cc)

target_compile_options(server PRIVATE -Wno-narrowing)

if(CMAKE_BUILD_TYPE MATCHES Release)
  set(BRANCH "release")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif()

# This is the correct way to link
#ExternalProject_Add(verona-rt
#  SOURCE_DIR ${CMAKE_SOURCE_DIR}/../external/verona-rt
#  PREFIX ${CMAKE_CURRENT_BINARY_DIR}/verona-rt
#)
#add_dependencies(server verona-rt)
#target_link_libraries(server INTERFACE verona_rt)

# verona-rt related
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/../external/verona-rt/src/rt)
# This shouldn't be a submodule
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/../external/snmalloc/src)
# enable external threading in verona-rt
target_compile_definitions(server PRIVATE -DVERONA_EXTERNAL_THREADING)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR})

# DPDK related
# Link DPDK
find_package(PkgConfig REQUIRED)
# set(ENV{PKG_CONFIG_PATH} ${CMAKE_BINARY_DIR}/dpdk-install-dir/lib/aarch64-linux-gnu/pkgconfig)
set(ENV{PKG_CONFIG_PATH} ${CMAKE_SOURCE_DIR}/../build/dpdk-install-dir/lib/x86_64-linux-gnu/pkgconfig)
pkg_check_modules(LIBDPDK REQUIRED libdpdk)

# SPSCQueue
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/../external/SPSCQueue/include/rigtorp)

target_compile_options(server PRIVATE ${LIBDPDK_STATIC_CFLAGS})
target_link_libraries(server ${LIBDPDK_STATIC_LDFLAGS})
