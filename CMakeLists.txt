cmake_minimum_required(VERSION 3.5.1)
project(Proxy)

set(CMAKE_CXX_STANDARD 14)

FIND_PACKAGE( Boost 1.40 COMPONENTS program_options REQUIRED )
INCLUDE_DIRECTORIES( ${Boost_INCLUDE_DIR} )

set(SOURCE_FILES main.cpp proxy.h server.cpp server.h socket.cpp socket.h )
add_executable(Proxy ${SOURCE_FILES})

TARGET_LINK_LIBRARIES( Proxy LINK_PUBLIC ${Boost_LIBRARIES} )