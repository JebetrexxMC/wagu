# mpgns: reliable + unreliable UDP lanes test on Valve GameNetworkingSockets
# (fork: github.com/JebetrexxMC/GameNetworkingSockets). Needs protobuf + OpenSSL:
#   sudo apt install libprotobuf-dev protobuf-compiler libssl-dev
#   cmake -S net/gns -B net/gns/build -G Ninja && cmake --build net/gns/build && net/gns/build/mpgns
cmake_minimum_required(VERSION 3.16)
project(mpgns CXX C)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB ON CACHE BOOL "" FORCE)
FetchContent_Declare(gns
  GIT_REPOSITORY https://github.com/JebetrexxMC/GameNetworkingSockets.git
  GIT_TAG master
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(gns)

add_executable(mpgns mpgns.cpp)
target_link_libraries(mpgns PRIVATE GameNetworkingSockets::static)
