cmake_minimum_required(VERSION 3.25)
project(ProjetReseaux C)

set(CMAKE_C_STANDARD 11)

add_executable(ProjetReseaux main.c)

include_directories(/usr/include)
link_directories(/usr/lib/x86_64-linux-gnu/libsqlite3.so)
find_package(SQLite3 REQUIRED)
target_link_libraries(ProjetReseaux PRIVATE SQLite::SQLite3)

find_package(OpenSSL REQUIRED)
link_directories(/usr/lib/x86_64-linux-gnu/libssl.so)
include_directories(/usr/include/openssl)
target_link_libraries(ProjetReseaux PRIVATE OpenSSL::SSL OpenSSL::Crypto)