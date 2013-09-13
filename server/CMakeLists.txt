cmake_minimum_required(VERSION 2.8)

project(dffplayer_server)

find_package(Qt4 REQUIRED QtCore QtGui QtNetwork)
include(${QT_USE_FILE})

qt4_wrap_cpp(MOC_SRCS server.h)

add_executable(dffserver server.cpp ${MOC_SRCS})
target_link_libraries(dffserver ${QT_LIBRARIES})
