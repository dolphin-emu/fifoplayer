#ifndef FIFOPLAYER_PROTOCOL_H
#define FIFOPLAYER_PROTOCOL_H

#define DFF_CONN_PORT 15342

#define CMD_HANDSHAKE 0x00  // First command ever sent; used for exchanging version and handshake tokens
#define CMD_STREAM_DFF 0x01  // Start uploading a new fifo log
#define CMD_RUN_DFF 0x02  // Run most recently uploaded fifo log
#define CMD_SET_CONTEXT_FRAME 0x03  // Specify what frame the following communication is referring to
#define CMD_SET_CONTEXT_COMMAND 0x04  // Specify what command the following communication is referring to
#define CMD_ENABLE_COMMAND 0x05 // Enable current command
#define CMD_DISABLE_COMMAND 0x06 // Enable current command

#define RET_FAIL 0
#define RET_SUCCESS 1
#define RET_WOULDBLOCK 2

static const int version = 0;
static const uint32_t handshake = 0x6fe62ac7;
static const int dff_stream_chunk_size = 4096; // in bytes

// TODO: Move client side here, too!

// TODO: Move client and server side to different files...


#endif // FIFOPLAYER_PROTOCOL_H
