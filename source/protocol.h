#ifndef FIFOPLAYER_PROTOCOL_H
#define FIFOPLAYER_PROTOCOL_H

#include <stdint.h>

#define DFF_CONN_PORT 15342

#define CMD_HANDSHAKE 0x00  // First command ever sent; used for exchanging version and handshake tokens
#define CMD_STREAM_DFF 0x01  // Start uploading a new fifo log
#define CMD_RUN_DFF 0x02  // Run most recently uploaded fifo log
#define CMD_SET_CONTEXT_FRAME 0x03  // Specify what frame the following communication is referring to
#define CMD_SET_CONTEXT_COMMAND 0x04  // Specify what command the following communication is referring to
#define CMD_ENABLE_COMMAND 0x05 // Enable current command
#define CMD_DISABLE_COMMAND 0x06 // Enable current command
#define CMD_PATCH_COMMAND 0x07 // Patch a certain command

#define RET_FAIL 0
#define RET_SUCCESS 1
#define RET_WOULDBLOCK 2

static const int version = 0;
static const uint32_t handshake = 0x6fe62ac7;
static const int dff_stream_chunk_size = 4096; // in bytes

// TODO: Move client side here, too!

// TODO: Move client and server side to different files...

void ReadStreamedDff(int socket);
int WaitForConnection(int& server_socket);

#define MSG_PEEK 0x02

#include <vector>

struct AnalyzedFrameInfo;
struct FifoFrameData;

int ReadHandshake(int socket);
void ReadStreamedDff(int socket, bool (*recv_callback)(void)); // if the callback returns true, the function will abort streaming
void ReadCommandEnable(int socket, std::vector<AnalyzedFrameInfo>& analyzed_frames, bool enable);
void ReadCommandPatch(int socket, std::vector<FifoFrameData>& frames);
void CheckForNetworkEvents(int server_socket, int client_socket, std::vector<FifoFrameData>& frames, std::vector<AnalyzedFrameInfo>& analyzed_frames);

#endif // FIFOPLAYER_PROTOCOL_H
