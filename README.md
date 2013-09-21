fifoplayer
==========

Wii homebrew application used for playing back dff files recorded by Dolphin on real hardware. Also includes a client which is used to control the application over network from a computer.


Usage
-----

Run the main executable via wiiload or from the Homebrew Channel. The application will start a server on the Wii which will listen to any connecting clients. When starting the client you'll need to enter you Wii's IP and select a dff file to run. Press "Connect" to establish a connection to the server application and finally "Load" to upload the dff file.

Note that both the client computer and the Wii need to have network access to each other over port 15342 for the communication to work.

The Wii application can be quitted by pressing the home button on your Wii Remote. Note that currently this will only work after successfully having established a connection to any client.

Apart from controlling the Wii application, the client GUI can also be used to analyze the structure of dff files. It's possible to disable individual commands such that one can restrict rendering to the relevant parts of the scene.


Compiling
---------

Like any homebrew application, the native fifoplayer can be compiled and run by calling "make && make run" from the root project directory. The client application in the "dffclient" directory requires Qt and can be built with CMake.
