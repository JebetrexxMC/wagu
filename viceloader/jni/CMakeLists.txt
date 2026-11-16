# VICE Loader native part (libviceloader.so): Dear ImGui (the San Andreas MP launcher look) on a GLES3 overlay,
# the MP virtual keyboard + emoji, the server list / LAN discovery and MP's own server (the VICE City LAN server).
cmake_minimum_required(VERSION 3.10)
project(viceloader C CXX)
set(W ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(CMAKE_CXX_STANDARD 17)
set(COMMON_FLAGS "-O2 -DNDEBUG -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-everything")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMMON_FLAGS} -fexceptions -frtti")

file(GLOB MBED_SRC ${W}/third_party/mbedtls/library/*.c)
add_library(vl_mbedtls STATIC ${MBED_SRC})
target_include_directories(vl_mbedtls PRIVATE ${W}/third_party/mbedtls/include ${W}/third_party/mbedtls/library)

add_library(vl_samp STATIC ${W}/third_party/pawn-amx/amx.c ${W}/third_party/pawn-amx/amxcore.c ${W}/third_party/pawn-amx/float.c
    ${W}/third_party/pawn-amx/amxstring.c ${W}/third_party/pawn-amx/amxaux.c ${W}/third_party/sqlite/sqlite3.c ${W}/third_party/puff/puff.c)
target_compile_definitions(vl_samp PRIVATE HAVE_STDINT_H AMX_ANSIONLY AMX_NODYNALOAD AMX_DONT_RELOCATE FLOATPOINT stricmp=strcasecmp SQLITE_OMIT_LOAD_EXTENSION SQLITE_THREADSAFE=1)

set(UNRAR_FILES rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode system crypt crc rawread encname resource
    match timefn rdwrfn consio options errhnd rarvm secpassword rijndael getbits sha1 sha256 blake2s hash extinfo extract volume list find unpack
    headers threadpool rs16 cmddata ui largepage filestr scantree dll qopen)
set(UNRAR_SRC "")
foreach(f ${UNRAR_FILES})
  list(APPEND UNRAR_SRC ${W}/third_party/unrar/${f}.cpp)
endforeach()
add_library(vl_unrar STATIC ${UNRAR_SRC})
target_compile_definitions(vl_unrar PRIVATE RARDLL _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE)
set_target_properties(vl_unrar PROPERTIES CXX_STANDARD 11)

add_library(viceloader SHARED
    vl_main.cpp vl_panel.cpp vl_shims.cpp vl_crash.cpp
    backends/imgui_impl_opengl3.cpp
    ${W}/imgui/imgui.cpp ${W}/imgui/imgui_draw.cpp ${W}/imgui/imgui_tables.cpp ${W}/imgui/imgui_widgets.cpp
    ${W}/src/ui/vkeyboard.cpp ${W}/src/ui/emoji.cpp
    ${W}/src/net/serverlist.cpp ${W}/src/net/lan.cpp
    ${W}/src/net/server.cpp ${W}/src/net/online.cpp ${W}/src/net/authcrypto.cpp ${W}/src/net/samp_import.cpp
    ${W}/include/mod/logger.cpp)
target_include_directories(viceloader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${W}/include ${W}/imgui ${W}/src ${W}/src/ui ${W}/third_party/mbedtls/include)
target_compile_definitions(viceloader PRIVATE IMGUI_IMPL_OPENGL_ES3 IMGUI_DISABLE_OBSOLETE_FUNCTIONS HAVE_STDINT_H AMX_ANSIONLY AMX_NODYNALOAD
    AMX_DONT_RELOCATE FLOATPOINT VICE_LOADER)
target_link_libraries(viceloader vl_unrar vl_samp vl_mbedtls GLESv3 EGL android log m dl)
target_link_options(viceloader PRIVATE -Wl,--gc-sections -Wl,--exclude-libs,ALL -static-libstdc++)
