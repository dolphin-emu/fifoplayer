fifoplayer
==========

GC/Wii homebrew application used for playing back dff files recorded by Dolphin on real hardware.

Usage
-----

Currently, the source dff file needs to be hardcoded in the source. Scroll down in main.cpp and change the "#define DFF_FILENAME" line to point to the dff file that you want to play back.

Like any homebrew application, the native fifoplayer can be compiled and run by calling "make && make run".
