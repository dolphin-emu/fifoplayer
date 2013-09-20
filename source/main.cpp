#define ENABLE_CONSOLE 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>
#include <machine/endian.h>
#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <unistd.h>
#include <fat.h>
#include <dirent.h>
#include <network.h>
#include "protocol.h"
#include "BPMemory.h"
#include "DffFile.h"
#include "FifoDataFile.h"
#include "OpcodeDecoding.h"
#include "FifoAnalyzer.h"


typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;


#define DEF_ALIGN 32
class aligned_buf
{
public:
	aligned_buf() : buf(NULL), size(0), alignment(DEF_ALIGN) {}
	aligned_buf(int alignment) : buf(NULL), size(0), alignment(alignment) {}
	~aligned_buf()
	{
		free(buf);
	}

	aligned_buf(const aligned_buf& oth)
	{
		if (oth.buf)
		{
			buf = (u8*)memalign(oth.alignment, oth.size);
			printf("copied to %p (%x) \n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf));
			memcpy(buf, oth.buf, oth.size);
		}
		else buf = NULL;
		size = oth.size;
		alignment = oth.alignment;
	}

	void resize(int new_size)
	{
		if (!buf)
		{
			buf = (u8*)memalign(alignment, new_size);
			printf("allocated to %p (%x) - size %x \n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf), new_size);

		}
		else
		{
			u8* old_buf = buf;
			buf = (u8*)memalign(alignment, new_size);
			memcpy(buf, old_buf, std::min(new_size, size));
			printf("reallocated to %p (%x)\n", buf, MEM_VIRTUAL_TO_PHYSICAL(buf));
			free(old_buf);
		}
		size = new_size;
	}

	u8* buf;
	int size;

private:
	int alignment;
};

std::map<u32, aligned_buf > memory_map; // map of memory chunks (indexed by starting address)

bool IntersectsMemoryRange(u32 start1, u32 size1, u32 start2, u32 size2)
{
	return size1 && size2 && ((start1 >= start2 && start1 < start2 + size2) ||
			(start2 >= start1 && start2 < start1 + size1));
}

// TODO: Needs to take care of alignment, too!
// Returns true if memory layout changed
bool PrepareMemoryLoad(u32 start_addr, u32 size)
{
	bool ret = false;

	// Make sure alignment of data inside the memory block is preserved
	u32 off = start_addr % DEF_ALIGN;
	start_addr = start_addr - off;
	size += off;

	std::vector<u32> affected_elements;
	u32 new_start_addr = start_addr;
	u32 new_end_addr = start_addr + size - 1;

	// Find overlaps with existing memory chunks
	for (auto it = memory_map.begin(); it != memory_map.end(); ++it)
	{
		if (IntersectsMemoryRange(it->first, it->second.size, start_addr, size))
		{
			affected_elements.push_back(it->first);
			if (it->first < new_start_addr)
				new_start_addr = it->first;
			if (it->first + it->second.size > new_end_addr + 1)
				new_end_addr = it->first + it->second.size - 1;
		}
	}

	aligned_buf& new_memchunk(memory_map[new_start_addr]); // creates a new vector or uses the existing one
	u32 new_size = new_end_addr - new_start_addr + 1;

	// if the new memory range is inside an existing chunk, there's nothing to do
	if (new_memchunk.size == new_size)
		return false;

	// resize chunk to required size, move old content to it, replace old arrays with new one
	// NOTE: can't do reserve here because not the whole memory might be covered by existing memory chunks
	new_memchunk.resize(new_size);
	while (!affected_elements.empty())
	{
		u32 addr = affected_elements.back();

		// first chunk is already in new_memchunk
		if (addr != new_start_addr)
		{
			aligned_buf& src = memory_map[addr];
			memcpy(&new_memchunk.buf[addr - new_start_addr], &src.buf[0], src.size);
			memory_map.erase(addr);

			ret = true;
		}
		affected_elements.pop_back();
	}

	// TODO: Handle critical case where memory allocation fails!

	return ret;
}

// Must have been reserved via PrepareMemoryLoad first
u8* GetPointer(u32 addr)
{
	for (auto it = memory_map.begin(); it != memory_map.end(); ++it)
		if (addr >= it->first && addr < it->first + it->second.size)
			return &it->second.buf[addr - it->first];

	return NULL;
}

static u32 tex_addr[8] = {0};

void ApplyInitialState(const FifoData& fifo_data, u32* tex_addr, CPMemory& target_cpmem)
{
	const std::vector<u32>& bpmem = fifo_data.bpmem;
	const std::vector<u32>& cpmem = fifo_data.cpmem;
	const std::vector<u32>& xfmem = fifo_data.xfmem;
	const std::vector<u32>& xfregs = fifo_data.xfregs;

	for (unsigned int i = 0; i < fifo_data.bpmem.size(); ++i)
	{
		if ((i == BPMEM_TRIGGER_EFB_COPY
			|| i == BPMEM_CLEARBBOX1
			|| i == BPMEM_CLEARBBOX2
			|| i == BPMEM_SETDRAWDONE
			|| i == BPMEM_PE_TOKEN_ID // TODO: Sure that we want to skip this one?
			|| i == BPMEM_PE_TOKEN_INT_ID
			|| i == BPMEM_LOADTLUT0
			|| i == BPMEM_LOADTLUT1
			|| i == BPMEM_TEXINVALIDATE
			|| i == BPMEM_PRELOAD_MODE
			|| i == BPMEM_CLEAR_PIXEL_PERF))
			continue;

		u32 new_value = bpmem[i];

		// Patch texture addresses
		if ((i >= BPMEM_TX_SETIMAGE3 && i < BPMEM_TX_SETIMAGE3+4) ||
			(i >= BPMEM_TX_SETIMAGE3_4 && i < BPMEM_TX_SETIMAGE3_4+4))
		{
			u32 tempval = le32toh(new_value);
			TexImage3* img = (TexImage3*)&tempval;
			u32 addr = img->image_base << 5;
			u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
			img->image_base = new_addr >> 5;
			new_value = h32tole(tempval);

			if (tex_addr)
			{
				if (i >= BPMEM_TX_SETIMAGE3 && i < BPMEM_TX_SETIMAGE3+4)
					tex_addr[i - BPMEM_TX_SETIMAGE3] = new_addr;
				else
					tex_addr[4 + i - BPMEM_TX_SETIMAGE3_4] = new_addr;
			}
		}

#if ENABLE_CONSOLE!=1
		wgPipe->U8 = 0x61;
		wgPipe->U32 = (i<<24)|(le32toh(new_value)&0xffffff);
#endif
	}

#if ENABLE_CONSOLE!=1
	#define MLoadCPReg(addr, val) { wgPipe->U8 = 0x08; wgPipe->U8 = addr; wgPipe->U32 = val; LoadCPReg(addr, le32toh(cpmem[addr]), target_cpmem); }

	MLoadCPReg(0x30, le32toh(cpmem[0x30]));
	MLoadCPReg(0x40, le32toh(cpmem[0x40]));
	MLoadCPReg(0x50, le32toh(cpmem[0x50]));
	MLoadCPReg(0x60, le32toh(cpmem[0x60]));

	for (int i = 0; i < 8; ++i)
	{
		MLoadCPReg(0x70 + i, le32toh(cpmem[0x70 + i]));
		MLoadCPReg(0x80 + i, le32toh(cpmem[0x80 + i]));
		MLoadCPReg(0x90 + i, le32toh(cpmem[0x90 + i]));
	}

	for (int i = 0; i < 16; ++i)
	{
		MLoadCPReg(0xa0 + i, le32toh(cpmem[0xa0 + i]));
		MLoadCPReg(0xb0 + i, le32toh(cpmem[0xb0 + i]));
	}
	#undef MLoadCPReg

	for (unsigned int i = 0; i < xfmem.size(); i += 16)
	{
		wgPipe->U8 = 0x10;
		wgPipe->U32 = 0xf0000 | (i&0xffff); // load 16*4 bytes
		for (int k = 0; k < 16; ++k)
			wgPipe->U32 = le32toh(xfmem[i + k]);
	}

	for (unsigned int i = 0; i < xfregs.size(); ++i)
	{
		wgPipe->U8 = 0x10;
		wgPipe->U32 = 0x1000 | (i&0x0fff);
		wgPipe->U32 = le32toh(xfregs[i]);
	}

	// Flush WGP
	for (int i = 0; i < 7; ++i)
		wgPipe->U32 = 0;
	wgPipe->U16 = 0;
	wgPipe->U8 = 0;
#endif
}

#define DFF_FILENAME "sd:/dff/test.dff"

#define DEFAULT_FIFO_SIZE   (256*1024)
static void *frameBuffer[2] = { NULL, NULL};
GXRModeObj *rmode;

u32 fb = 0;
u32 first_frame = 1;

void Init()
{
	VIDEO_Init();

	rmode = VIDEO_GetPreferredMode(NULL);
	first_frame = 1;
	fb = 0;
	frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)); // TODO: Shouldn't require manual framebuffer management!
	frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(frameBuffer[fb]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
	fb ^= 1;

	void *gp_fifo = NULL;
	gp_fifo = memalign(32,DEFAULT_FIFO_SIZE);
	memset(gp_fifo,0,DEFAULT_FIFO_SIZE);

	GX_Init(gp_fifo,DEFAULT_FIFO_SIZE);

#if ENABLE_CONSOLE==1
    console_init(frameBuffer[0],20,20,rmode->fbWidth,rmode->xfbHeight,rmode->fbWidth*VI_DISPLAY_PIX_SZ);
#endif

	WPAD_Init();

	if(!fatInitDefault())
	{
		printf("fatInitDefault failed!\n");
	}

	net_init();
}

#include "mygx.h"

int ReadHandshake(int socket)
{
	char data[4];
	net_recv(socket, data, sizeof(data), 0);
	uint32_t received_handshake = ntohl(*(uint32_t*)&data[0]);

	if (received_handshake != handshake)
		return RET_FAIL;

	return RET_SUCCESS;
}

bool CheckIfHomePressed()
{
/*	VIDEO_WaitVSync();
	fb ^= 1;
*/
	WPAD_ScanPads();

	if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
	{
		return true;
	}
	return false;
}

void ReadStreamedDff(int socket)
{
	int32_t n_size;
	net_recv(socket, &n_size, 4, 0);
	int32_t size = ntohl(n_size);
	printf("About to read %d bytes of dff data!", size);

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
		CheckIfHomePressed();
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


void CheckForNetworkEvents(int server_socket, int client_socket, std::vector<AnalyzedFrameInfo>& analyzed_frames)
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

			default:
				printf("Received unknown command: %d\n", cmd);
				break;
		}
		printf("Looping again\n");
		timeout = 100;
	} while (ret > 0);
}

int main()
{
	Init();

	printf("Init done!\n");
	int server_socket;
	int client_socket = WaitForConnection(server_socket);
	u8 dummy;
	net_recv(client_socket, &dummy, 1, 0);
	if (RET_SUCCESS == ReadHandshake(client_socket))
		printf("Successfully exchanged handshake token!\n");
	else
		printf("Failed to exchanged handshake token!\n");

	net_recv(client_socket, &dummy, 1, 0);
	ReadStreamedDff(client_socket);

	FifoData fifo_data;
	LoadDffData(DFF_FILENAME, fifo_data);
	printf("Loaded dff data\n");

	FifoDataAnalyzer analyzer;
	std::vector<AnalyzedFrameInfo> analyzed_frames;
	analyzer.AnalyzeFrames(fifo_data, analyzed_frames);
	printf("Analyzed dff data\n");

	CPMemory cpmem; // TODO: Should be removed...

	bool processing = true;
	int first_frame = 0;
	int last_frame = first_frame + fifo_data.frames.size()-1;
	int cur_frame = first_frame;
	while (processing)
	{
		CheckForNetworkEvents(server_socket, client_socket, analyzed_frames);
	printf("Done with that one...\n");

		FifoFrameData& cur_frame_data = fifo_data.frames[cur_frame];
		AnalyzedFrameInfo& cur_analyzed_frame = analyzed_frames[cur_frame];
		if (cur_frame == 0) // TODO: Check for first_frame instead and apply previous state changes
		{
			for (unsigned int frameNum = 0; frameNum < fifo_data.frames.size(); ++frameNum)
			{
				const FifoFrameData &frame = fifo_data.frames[frameNum];
				for (unsigned int i = 0; i < frame.memoryUpdates.size(); ++i)
				{
					PrepareMemoryLoad(frame.memoryUpdates[i].address, frame.memoryUpdates[i].dataSize);
					//if (early_mem_updates)
					//	memcpy(GetPointer(frame.memoryUpdates[i].address), &frame.memoryUpdates[i].data[0], frame.memoryUpdates[i].data.size());
					//DCFlushRange(GetPointer(frame.memoryUpdates[i].address), frame.memoryUpdates[i].dataSize);
				}
			}

			ApplyInitialState(fifo_data, tex_addr, cpmem);
		}

		u32 last_pos = 0;
		for (std::vector<AnalyzedObject>::iterator cur_object = cur_analyzed_frame.objects.begin();
			cur_object != cur_analyzed_frame.objects.end(); ++cur_object)
		{
			for (std::vector<u32>::iterator cur_command = cur_object->cmd_starts.begin();
				cur_command != cur_object->cmd_starts.end(); ++cur_command)
			{
				const int cmd_index = cur_command-cur_object->cmd_starts.begin();
				u8* cmd_data = &cur_frame_data.fifoData[*cur_command];

				const FifoFrameData &frame = fifo_data.frames[cur_frame];
				for (unsigned int update = 0; update < frame.memoryUpdates.size(); ++update)
				{
					if ((!last_pos || frame.memoryUpdates[update].fifoPosition > last_pos) && frame.memoryUpdates[update].fifoPosition <= *cur_command)
					{
	//					PrepareMemoryLoad(frame.memoryUpdates[update].address, frame.memoryUpdates[update].dataSize);
						fseek(fifo_data.file, frame.memoryUpdates[update].dataOffset, SEEK_SET);
						fread(GetPointer(frame.memoryUpdates[update].address), frame.memoryUpdates[update].dataSize, 1, fifo_data.file);

						// DCFlushRange expects aligned addresses
						u32 off = frame.memoryUpdates[update].address % DEF_ALIGN;
						DCFlushRange(GetPointer(frame.memoryUpdates[update].address - off), frame.memoryUpdates[update].dataSize+off);
					}
				}
				last_pos = *cur_command;

				static u32 efbcopy_target = 0;

				if (!cur_object->cmd_enabled[cmd_index])
					continue;

				if (cmd_data[0] == GX_LOAD_BP_REG)
				{
					// Patch texture addresses
					if ((cmd_data[1] >= BPMEM_TX_SETIMAGE3 && cmd_data[1] < BPMEM_TX_SETIMAGE3+4) ||
						(cmd_data[1] >= BPMEM_TX_SETIMAGE3_4 && cmd_data[1] < BPMEM_TX_SETIMAGE3_4+4))
					{
						u32 tempval = /*be32toh*/(*(u32*)&cmd_data[1]);
						TexImage3* img = (TexImage3*)&tempval;
						u32 addr = img->image_base << 5; // TODO: Proper mask?
						u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
						img->image_base = new_addr >> 5;
						u32 new_value = /*h32tobe*/(tempval);

#if ENABLE_CONSOLE!=1
						wgPipe->U8 = 0x61;
						wgPipe->U32 = ((u32)cmd_data[1]<<24)|(/*be32toh*/(new_value)&0xffffff);
#endif

						if (cmd_data[1] >= BPMEM_TX_SETIMAGE3 &&
							cmd_data[1] < BPMEM_TX_SETIMAGE3+4)
							tex_addr[cmd_data[1] - BPMEM_TX_SETIMAGE3] = new_addr;
						else
							tex_addr[4 + cmd_data[1] - BPMEM_TX_SETIMAGE3_4] = new_addr;
					}
					else if (cmd_data[1] == BPMEM_PRELOAD_ADDR)
					{
						// TODO
#if ENABLE_CONSOLE!=1
						wgPipe->U8 = 0x61;
						wgPipe->U8 = cmd_data[1];
						wgPipe->U8 = cmd_data[2];
						wgPipe->U8 = cmd_data[3];
						wgPipe->U8 = cmd_data[4];
#endif
					}
					else if (cmd_data[1] == BPMEM_LOADTLUT0)
					{
#if 1
						// TODO: Untested
						u32 tempval = /*be32toh*/(*(u32*)&cmd_data[1]);
						u32 addr = tempval << 5; // TODO: Proper mask?
						u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
						u32 new_value = new_addr >> 5;

#if ENABLE_CONSOLE!=1
						wgPipe->U8 = cmd_data[0];
						wgPipe->U32 = (BPMEM_LOADTLUT0<<24)|(/*be32toh*/(new_value)&0xffffff);
#endif
#endif
					}
					else if (cmd_data[1] == BPMEM_EFB_ADDR)
					{
						u32 tempval = /*be32toh*/(*(u32*)&cmd_data[1]);
						u32 addr = (tempval & 0xFFFFFF) << 5; // TODO
						efbcopy_target = addr;

#if ENABLE_CONSOLE!=1
						wgPipe->U8 = 0x61;
						wgPipe->U8 = cmd_data[1];
						wgPipe->U8 = cmd_data[2];
						wgPipe->U8 = cmd_data[3];
						wgPipe->U8 = cmd_data[4];
#endif
					}
					else if (cmd_data[1] == BPMEM_TRIGGER_EFB_COPY)
					{
						u32 tempval = /*be32toh*/(*(u32*)&cmd_data[1]);
						UPE_Copy* copy = (UPE_Copy*)&tempval;
						if (!copy->copy_to_xfb)
						{
							bool update_textures = PrepareMemoryLoad(efbcopy_target, 640*480*4); // TODO: Size!!
							u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(efbcopy_target));
							u32 new_value = /*h32tobe*/((BPMEM_EFB_ADDR<<24) | (new_addr >> 5));

							// Update target address
#if ENABLE_CONSOLE!=1
							wgPipe->U8 = 0x61;
							wgPipe->U32 = (BPMEM_EFB_ADDR<<24)|(/*be32toh*/(new_value)&0xffffff);
#endif

							// Gotta fix texture offsets if memory map layout changed
							if (update_textures)
							{
								for (int k = 0; k < 8; ++k)
								{
									u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(tex_addr[k]));
									u32 new_value = /*h32tobe*/(new_addr>>5);

#if ENABLE_CONSOLE!=1
									wgPipe->U8 = 0x61;
									u32 reg = (k < 4) ? (BPMEM_TX_SETIMAGE3+k) : (BPMEM_TX_SETIMAGE3_4+(k-4));
									wgPipe->U32 = (reg<<24)|(/*be32toh*/(new_value)&0xffffff);
#endif
								}
							}
						}
					}
					else
					{
#if ENABLE_CONSOLE!=1
						wgPipe->U8 = 0x61;
						wgPipe->U8 = cmd_data[1];
						wgPipe->U8 = cmd_data[2];
						wgPipe->U8 = cmd_data[3];
						wgPipe->U8 = cmd_data[4];
#endif
					}
				}
				else if (cmd_data[0] == GX_LOAD_CP_REG)
				{
					u8 cmd2 = cmd_data[1];
					u32 value = *(u32*)&cmd_data[2]; // TODO: Endiannes (only works on Wii)
					if ((cmd2 & 0xF0) == 0xA0) // TODO: readability!
						value = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(value));

#if ENABLE_CONSOLE!=1
					wgPipe->U8 = GX_LOAD_CP_REG;
					wgPipe->U8 = cmd_data[1];
					wgPipe->U32 = value;
#endif

					LoadCPReg(cmd2, value, cpmem);
				}
				else if (cmd_data[0] == GX_LOAD_XF_REG)
				{
					// Load data directly instead of going through the loop again for no reason

					u32 cmd2 = *(u32*)&cmd_data[1]; // TODO: Endianness (only works on Wii)
					u8 streamSize = ((cmd2 >> 16) & 15) + 1;

#if ENABLE_CONSOLE!=1
					wgPipe->U8 = GX_LOAD_XF_REG;
					wgPipe->U32 = cmd2;

					for (int byte = 0; byte < streamSize * 4; ++byte)
						wgPipe->U8 = cmd_data[5+byte];
#endif
				}
				else if(cmd_data[0] == GX_LOAD_INDX_A ||
						cmd_data[0] == GX_LOAD_INDX_B ||
						cmd_data[0] == GX_LOAD_INDX_C ||
						cmd_data[0] == GX_LOAD_INDX_D)
				{
#if ENABLE_CONSOLE!=1
					wgPipe->U8 = cmd_data[0];
					wgPipe->U8 = cmd_data[1];
					wgPipe->U8 = cmd_data[2];
					wgPipe->U8 = cmd_data[3];
					wgPipe->U8 = cmd_data[4];
#endif
				}
				else if (cmd_data[0] & 0x80)
				{
					u32 vtxAttrGroup = cmd_data[0] & GX_VAT_MASK;
					int vertexSize = CalculateVertexSize(vtxAttrGroup, cpmem);

					u16 streamSize = *(u16*)&cmd_data[1]; // TODO: Endianness (only works on Wii)

#if ENABLE_CONSOLE!=1
					wgPipe->U8 = cmd_data[0];
					wgPipe->U16 = streamSize;
					for (int byte = 0; byte < streamSize * vertexSize; ++byte)
						wgPipe->U8 = cmd_data[3+byte];
#endif
				}
				else
				{
					u32 size = cur_object->last_cmd_byte - *cur_command + 1;
					if (cur_command+1 != cur_object->cmd_starts.end() && size > *(cur_command+1)-*cur_command)
						size = *(cur_command+1)-*cur_command;

#if ENABLE_CONSOLE!=1
					for (u32 addr = 0; addr < size; ++addr) {
						// TODO: Push u32s instead
						wgPipe->U8 = cmd_data[addr];
					}
#endif
				}
			}
		}

		// TODO: Flush WGPipe

#if ENABLE_CONSOLE!=1
		// finish frame
		// Note that GX_CopyDisp(frameBuffer[fb],GX_TRUE) uses an internal state
		// which is out of sync with the dff_data, so we're manually writing
		// to the EFB copy registers instead.
		wgPipe->U8 = GX_LOAD_BP_REG;
		wgPipe->U32 = (BPMEM_EFB_ADDR << 24) | ((MEM_VIRTUAL_TO_PHYSICAL(frameBuffer[fb]) >> 5) & 0xFFFFFF);

		UPE_Copy copy;
		copy.Hex = 0;
		copy.clear = 1;
		copy.copy_to_xfb = 1;
		wgPipe->U8 = GX_LOAD_BP_REG;
		wgPipe->U32 = (BPMEM_TRIGGER_EFB_COPY << 24) | copy.Hex;

		VIDEO_SetNextFramebuffer(frameBuffer[fb]);
		if (first_frame)
		{
			VIDEO_SetBlack(FALSE);
			first_frame = 0;
		}

		VIDEO_Flush();
#endif
		VIDEO_WaitVSync();
		fb ^= 1;

		// TODO: Menu stuff
		// reset GX state
		// draw menu
		// restore GX state

		// input checking
		// A = select menu point
		// B = menu back
		// plus = pause
		// minus = hide menu
		// home = stop
		WPAD_ScanPads();

//		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
//			processing = false;

		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
		{
			printf("\n");
/*			for (unsigned int i = 0; i < fifo_data.frames[0].fifoData.size(); ++i)
			{
				printf("%02x", fifo_data.frames[0].fifoData[i]);
				if (i == fifo_data.frames[0].fifoData.size()-5) printf("_");
//				if ((i % 4) == 3) printf(" ");
//				if ((i % 16) == 15) printf("\n");
				if ((i % 4) == 3) printf(" ");
				if ((i % 24) == 23) printf("\n");
			}*/
			fclose(fifo_data.file);
			exit(0);
		}

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	fclose(fifo_data.file);

	return 0;
}
