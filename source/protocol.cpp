#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <network.h>
#include <dirent.h>

#include "FifoAnalyzer.h"
#include "FifoDataFile.h"

int ReadHandshake(int socket)
{
	char data[4];
	net_recv(socket, data, sizeof(data), 0);
	uint32_t received_handshake = ntohl(*(uint32_t*)&data[0]);

	if (received_handshake != handshake)
		return RET_FAIL;

	return RET_SUCCESS;
}

void ReadStreamedDff(int socket, bool (*recv_callback)(void))
{
	int32_t n_size;
	net_recv(socket, &n_size, 4, 0);
	int32_t size = ntohl(n_size);
	printf("About to read %d bytes of dff data!", size);

	mkdir("sd:/dff", 0777);
	FILE* file = fopen("sd:/dff/test.dff", "wb"); // TODO: Change!

	if (file == NULL)
	{
		printf("Failed to open output file!\n");
	}

	for (; size > 0; )
	{
		char data[dff_stream_chunk_size];
		ssize_t num_received = net_recv(socket, data, std::min(size,dff_stream_chunk_size), 0);
		if (num_received == -1)
		{
			printf("Error in recv!\n");
		}
		else if (num_received > 0)
		{
			fwrite(data, num_received, 1, file);
			size -= num_received;
		}
//		printf("%d bytes left to be read!\n", size);
		if (recv_callback())
		{
			printf ("Pressed Home button, aborting...\n");
			break;
		}
	}
	printf ("Done reading :)\n");

	fclose(file);
}

int WaitForConnection(int& server_socket)
{
	int addrlen;
	struct sockaddr_in my_name, peer_name;
	int status;

	server_socket = net_socket(AF_INET, SOCK_STREAM, 0);
	if (server_socket == -1)
	{
		printf("Failed to create server socket\n");
	}
	int yes = 1;
	net_setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

	memset(&my_name, 0, sizeof(my_name));
	my_name.sin_family = AF_INET;
	my_name.sin_port = htons(DFF_CONN_PORT);
	my_name.sin_addr.s_addr = htonl(INADDR_ANY);

	status = net_bind(server_socket, (struct sockaddr*)&my_name, sizeof(my_name));
	if (status == -1)
	{
		printf("Failed to bind server socket\n");
	}

	status = net_listen(server_socket, 5); // TODO: Change second parameter..
	if (status == -1)
	{
		printf("Failed to listen on server socket\n");
	}
	printf("Listening now!\n");

	int client_socket = -1;

	struct sockaddr_in client_info;
	socklen_t ssize = sizeof(client_info);
	int new_socket = net_accept(server_socket, (struct sockaddr*)&client_info, &ssize);
	if (new_socket < 0)
	{
		printf("accept failed!\n");
	}
	else
	{
		client_socket = new_socket;
		printf("accept succeeded and returned %d\n", client_socket);
	}

	return client_socket;
}

#define MSG_PEEK 0x02

void ReadCommandEnable(int socket, std::vector<AnalyzedFrameInfo>& analyzed_frames, bool enable)
{
	char cmd;
	u32 frame_idx;
	u32 object;
	u32 offset;

	char data[12];

	ssize_t numread = 0;
	while (numread != sizeof(data))
		numread += net_recv(socket, data+numread, sizeof(data)-numread, 0);

	frame_idx = ntohl(*(u32*)&data[0]);
	object = ntohl(*(u32*)&data[4]);
	offset = ntohl(*(u32*)&data[8]);

	printf("%s command %d in frame %d;\n", (enable)?"Enabled":"Disabled", offset, frame_idx);
	AnalyzedFrameInfo& frame = analyzed_frames[frame_idx];
	AnalyzedObject& obj = frame.objects[object];

	for (int i = 0; i < obj.cmd_starts.size(); ++i)
	{
		if (obj.cmd_starts[i] == offset)
		{
			obj.cmd_enabled[i] = enable;
			printf("%s command %d in frame %d, %d\n", (enable)?"Enabled":"Disabled", i, frame_idx, obj.cmd_enabled.size());
			break;
		}
	}
}

void ReadCommandPatch(int socket, std::vector<FifoFrameData>& frames)
{
	char cmd;
	u32 frame_idx;
	u32 size;
	u32 offset;

	char data[12];

	ssize_t numread = 0;
	while (numread != sizeof(data))
		numread += net_recv(socket, data+numread, sizeof(data)-numread, 0);

	frame_idx = ntohl(*(u32*)&data[0]);
	offset = ntohl(*(u32*)&data[4]);
	size = ntohl(*(u32*)&data[8]);

	printf("Patching %d bytes of frame %d at offset %d;\n", size, frame_idx, offset);
	numread = 0;
	while (numread != size)
	{
		ssize_t numread_now = 0;
		if (0 != (numread_now = net_recv(socket, data, 1, 0)))
		{
			frames[frame_idx].fifoData[offset + numread] = data[0];
			++numread;
		}
	}

	// TODO: Need to update frame analysis here..
}


void CheckForNetworkEvents(int server_socket, int client_socket, std::vector<FifoFrameData>& frames, std::vector<AnalyzedFrameInfo>& analyzed_frames)
{
#if 0
	fd_set readset;
	FD_ZERO(&readset);
//	FD_SET(server_socket, &readset);
//	if (client_socket != -1)
		FD_SET(client_socket, &readset);
//	int maxfd = std::max(client_socket, server_socket);
	int maxfd = client_socket;

	struct timeval timeout;
	timeout.tv_sec = 1;
	timeout.tv_usec = 0;

	char data[12];
	int ret = net_select(maxfd+1, &readset, NULL, NULL, &timeout); // TODO: Is this compatible with winsocks?

	if (ret <= 0)
	{
		if (ret < 0)
			printf("select returned %d\n", ret);
		return;
	}
/*	if (FD_ISSET(server_socket, &readset))
	{
		int new_socket = net_accept(server_socket, NULL, NULL);
		if (new_socket < 0)
		{
			qDebug() << "accept failed";
		}
		else client_socket = new_socket;
	}*/
#endif

	struct pollsd fds[2];
	memset(fds, 0, sizeof(fds));
//	fds[0].socket = server_socket;
	fds[0].socket = client_socket;
	fds[0].events = POLLIN;
	int nfds = 1;
	int timeout = 1; // TODO: Set to zero

	int ret;
	do {
		ret = net_poll(fds, nfds, timeout);
		if (ret < 0)
		{
			printf("poll returned error %d\n", ret);
			return;
		}
		if (ret == 0)
		{
			printf("timeout :(\n");
			// timeout
			return;
		}

		char cmd;
		ssize_t numread = net_recv(client_socket, &cmd, 1, 0);
		printf("Peeked command %d\n", cmd);
		switch (cmd)
		{
			case CMD_HANDSHAKE:
				if (RET_SUCCESS == ReadHandshake(client_socket))
					printf("Successfully exchanged handshake token!\n");
				else
					printf("Failed to exchange handshake token!\n");

				// TODO: should probably write a handshake in return, but ... I'm lazy
				break;

			case CMD_STREAM_DFF:
				//ReadStreamedDff(client_socket);
				break;

			case CMD_ENABLE_COMMAND:
			case CMD_DISABLE_COMMAND:
				ReadCommandEnable(client_socket, analyzed_frames, (cmd == CMD_ENABLE_COMMAND) ? true : false);
				break;

			case CMD_PATCH_COMMAND:
				ReadCommandPatch(client_socket, frames);
				break;

			default:
				printf("Received unknown command: %d\n", cmd);
				break;
		}
		printf("Looping again\n");
		timeout = 100;
	} while (ret > 0);
}
