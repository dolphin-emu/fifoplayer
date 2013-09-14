cmake_minimum_required(VERSION 2.8)

project(dffplayer_server)

include(CheckIncludeFiles)
check_include_files(endian.h HAVE_ENDIAN_H)

set(CMAKE_CXX_FLAGS "-std=c++11")
if(HAVE_ENDIAN_H)
	add_definitions("-DHAVE_ENDIAN_H")
endif(HAVE_ENDIAN_H)

find_package(Qt4 REQUIRED QtCore QtGui QtNetwork)
include(${QT_USE_FILE})

qt4_wrap_cpp(MOC_SRCS server.h)

add_executable(dffserver server.cpp ../source/FifoDataFile.cpp ${MOC_SRCS})
target_link_libraries(dffserver ${QT_LIBRARIES})
