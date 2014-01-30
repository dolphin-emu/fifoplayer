cmake_minimum_required(VERSION 2.8)

project(dffclient)

include(CheckIncludeFiles)
check_include_files(endian.h HAVE_ENDIAN_H)

set(CMAKE_CXX_FLAGS "-std=c++11")
if(HAVE_ENDIAN_H)
	add_definitions("-DHAVE_ENDIAN_H")
endif(HAVE_ENDIAN_H)

find_package(Qt4 REQUIRED QtCore QtGui QtNetwork)
include(${QT_USE_FILE})

qt4_wrap_cpp(MOC_SRCS client.h command_info.h)

add_executable(dffclient client.cpp command_info.cpp ../source/FifoDataFile.cpp ../source/BPMemory.cpp ${MOC_SRCS})
target_link_libraries(dffclient ${QT_LIBRARIES})
