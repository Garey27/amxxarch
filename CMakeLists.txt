cmake_minimum_required(VERSION 3.10)
project(amxxarch)

set(CMAKE_CXX_STANDARD 11)

include_directories(include)
include_directories(include/cssdk/common)
include_directories(include/cssdk/dlls)
include_directories(include/cssdk/engine)
include_directories(include/cssdk/game_shared)
include_directories(include/cssdk/pm_shared)
include_directories(include/cssdk/public)
include_directories(include/metamod)
include_directories(unrar)
include_directories(src)
include_directories(src/mem)

find_library(LZO_LIBRARIES NAMES lzo2)
find_package(lz4 CONFIG REQUIRED)
find_package(LibXml2 REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(LibLZMA CONFIG REQUIRED)
find_path(LIBARCHIVE_INCLUDE_DIR archive.h)
find_library(LIBARCHIVE_LIBRARY archive)
include_directories(${LIBARCHIVE_INCLUDE_DIR})

add_library(amxxarch SHARED
        src/glibc_ver.cpp
        src/stdc++compat.cpp
        src/amxxmodule.cpp
        src/main.cpp)
		
add_subdirectory(unrar)

message(${LZO_LIBRARIES})
if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
    set(THREADS_PREFER_PTHREAD_FLAG ON)
    set(CMAKE_CXX_FLAGS     "${CMAKE_CXX_FLAGS} -m32 -D_vsnprintf=vsnprintf")
    set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g3")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}  -static -include ${PROJECT_SOURCE_DIR}/glibc_version_header/version_headers/x86/force_link_glibc_2.5.h --no-gnu-unique -fuse-ld=gold -static-libgcc -static-libstdc++ -Wl,--exclude-libs,libstdc++ -ldl -lrt -lm -lpthread -m32 -Wl,--whole-archive -lpthread -Wl,--no-whole-archive")
    set_target_properties(amxxarch PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32" POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(amxxarch PRIVATE ${LIBARCHIVE_LIBRARY} ${LZO_LIBRARIES} unrar ZLIB::ZLIB BZip2::BZip2 LibLZMA::LibLZMA ${LIBXML2_LIBRARIES} lz4::lz4 OpenSSL::Crypto OpenSSL::SSL)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -include ${PROJECT_SOURCE_DIR}/glibc_version_header/version_headers/x86/force_link_glibc_2.5.h")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include ${PROJECT_SOURCE_DIR}/glibc_version_header/version_headers/x86/force_link_glibc_2.5.h")    
    set(CMAKE_LINK_FLAGS "${CMAKE_LINK_FLAGS}")   
else()
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    set( CMAKE_SHARED_LINKER_FLAGS "/EXPORT:GiveFnptrsToDll=_GiveFnptrsToDll@8" )
    target_link_libraries(amxxarch PRIVATE ${LIBARCHIVE_LIBRARY} ${LZO_LIBRARIES} unrar ZLIB::ZLIB BZip2::BZip2 LibLZMA::LibLZMA ${LIBXML2_LIBRARIES} lz4::lz4 OpenSSL::Crypto OpenSSL::SSL crypt32.lib)
endif()

set_target_properties(amxxarch PROPERTIES PREFIX "")

if(CMAKE_CXX_COMPILER_ID MATCHES GNU)
set_target_properties(amxxarch PROPERTIES OUTPUT_NAME "amxxarch_amxx_i386")
else()
set_target_properties(amxxarch PROPERTIES OUTPUT_NAME "amxxarch_amxx")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/out)