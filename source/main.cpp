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

void ApplyInitialState(const FifoData& fifo_data, u32* tex_addr)
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
	#define MLoadCPReg(addr, val) { wgPipe->U8 = 0x08; wgPipe->U8 = addr; wgPipe->U32 = val; }

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


//#define DFF_FILENAME "sd:/dff/4_efbcopies_new.dff"
//#define DFF_FILENAME "sd:/dff/3_textures_new.dff"
//#define DFF_FILENAME "sd:/dff/5_mkdd.dff"
//#define DFF_FILENAME "sd:/dff/smg_marioeyes.dff"
#define DFF_FILENAME "sd:/dff/test.dff"
//#define DFF_FILENAME "sd:/dff/tmnt_fog.dff"
//#define DFF_FILENAME "sd:/dff/rs2_intro.dff"
//#define DFF_FILENAME "sd:/dff/simpletexture.dff"
//#define DFF_FILENAME "sd:/dff/fog_adj.dff"

struct AnalyzedFrameInfo
{
	std::vector<u32> object_starts; // Address of first command in a polygon rendering series
	std::vector<u32> object_ends; // Address of first command after rendering polygons

	// These two should be in a single vector, actually...
	std::vector<u32> cmd_starts; // Address of each command of the frame
	std::vector<bool> cmd_enabled; // Whether to process this command or not

//	std::vector<MemoryUpdate> memory_updates;
};

#include "OpcodeDecoding.h"
#include "FifoAnalyzer.h"


class FifoDataAnalyzer
{
public:
	void AnalyzeFrames(FifoData& data, std::vector<AnalyzedFrameInfo>& frame_info)
	{
		// TODO: Load BP mem

		u32 *cpMem = &data.cpmem[0];
		LoadCPReg(0x50, le32toh(cpMem[0x50]), m_cpmem);
		LoadCPReg(0x60, le32toh(cpMem[0x60]), m_cpmem);

		for (int i = 0; i < 8; ++i)
		{
			LoadCPReg(0x70 + i, le32toh(cpMem[0x70 + i]), m_cpmem);
			LoadCPReg(0x80 + i, le32toh(cpMem[0x80 + i]), m_cpmem);
			LoadCPReg(0x90 + i, le32toh(cpMem[0x90 + i]), m_cpmem);
		}

		frame_info.clear();
		frame_info.resize(data.frames.size());

		m_drawingObject = false;

		for (unsigned int frame_idx = 0; frame_idx < data.frames.size(); ++frame_idx)
		{
			FifoFrameData& src_frame = data.frames[frame_idx];
			AnalyzedFrameInfo& dst_frame = frame_info[frame_idx];

			u32 cmd_start = 0;

			while (cmd_start < src_frame.fifoData.size())
			{
				bool was_drawing = m_drawingObject;
				u32 cmd_size = DecodeCommand(&src_frame.fifoData[cmd_start]);

				// TODO: Check that cmd_size != 0

				if (was_drawing != m_drawingObject)
				{
					if (m_drawingObject)
						dst_frame.object_starts.push_back(cmd_start);
					else
						dst_frame.object_ends.push_back(cmd_start);
				}
				dst_frame.cmd_starts.push_back(cmd_start);
				dst_frame.cmd_enabled.push_back(true);
				cmd_start += cmd_size;
			}
			if (dst_frame.object_ends.size() < dst_frame.object_starts.size())
				dst_frame.object_ends.push_back(cmd_start);
		}
	}

	u32 DecodeCommand(u8* data)
	{
		u8* data_start = data;

		u8 cmd = ReadFifo8(data);

		static int stuff = 0;
//		printf("%02x ", cmd);
		++stuff;
//		if ((stuff % 16) == 15) printf("\n");
		switch (cmd)
		{
			case GX_NOP:
			case 0x44:
			case GX_CMD_INVL_VC:
				break;

			case GX_LOAD_CP_REG:
			{
				m_drawingObject = false;

				u32 cmd2 = ReadFifo8(data);
				u32 value = ReadFifo32(data);
				LoadCPReg(cmd2, value, m_cpmem);
				break;
			}

			case GX_LOAD_XF_REG:
			{
				m_drawingObject = false;

				u32 cmd2 = ReadFifo32(data);
				u8 stream_size = ((cmd2 >> 16) & 0xf) + 1; // TODO: Check if this works!

				data += stream_size * 4;
				break;
			}

			case GX_LOAD_INDX_A:
			case GX_LOAD_INDX_B:
			case GX_LOAD_INDX_C:
			case GX_LOAD_INDX_D:
				m_drawingObject = false;
				data += 4;
				break;

			case GX_CMD_CALL_DL:
				// The recorder should have expanded display lists into the fifo stream and skipped the call to start them
				// That is done to make it easier to track where memory is updated
				//_assert_(false);
				printf("Shouldn't have a DL here...\n");
				data += 8;
				break;

			case GX_LOAD_BP_REG:
			{
				m_drawingObject = false;

				u32 cmd2 = ReadFifo32(data);

//				printf("BP: %02x %08x\n", cmd, cmd2);
				//BPCmd bp = FifoAnalyzer::DecodeBPCmd(cmd2, m_BpMem); // TODO

				//FifoAnalyzer::LoadBPReg(bp, m_BpMem);
				// TODO: Load BP reg..

				// TODO
//				if (bp.address == BPMEM_TRIGGER_EFB_COPY)
//					StoreEfbCopyRegion();

				break;
			}

			default:
				if (cmd & 0x80)
				{
					m_drawingObject = true;
					u32 vtxAttrGroup = cmd & GX_VAT_MASK;
					int vertex_size = CalculateVertexSize(vtxAttrGroup, m_cpmem);

					u16 stream_size = ReadFifo16(data);
					data += stream_size * vertex_size;
				}
				else
				{
					printf("Invalid fifo command 0x%x\n", cmd);
					sleep(1);
				}
				break;
		}
		return data - data_start;
	}

private:
	bool m_drawingObject;

	CPMemory m_cpmem;
};

#pragma pack(pop)

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
	char data[5];
	net_recv(socket, data, sizeof(data), 0);
	uint32_t received_handshake = ntohl(*(uint32_t*)&data[1]);

	if (data[0] != CMD_HANDSHAKE || received_handshake != handshake)
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
	char cmd = CMD_STREAM_DFF;
	net_recv(socket, &cmd, 1, 0);

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

int WaitForConnection()
{
	int addrlen;
	struct sockaddr_in my_name, peer_name;
	int status;

	int server_socket = net_socket(AF_INET, SOCK_STREAM, 0);
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

int main()
{
	Init();

	printf("Init done!\n");
	int client_socket = WaitForConnection();
	if (RET_SUCCESS == ReadHandshake(client_socket))
		printf("Successfully exchanged handshake token!\n");
	else
		printf("Failed to exchanged handshake token!\n");

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

			ApplyInitialState(fifo_data, tex_addr);
		}

		std::vector<u32>::iterator next_cmd_start = cur_analyzed_frame.cmd_starts.begin();

		u32 last_pos = 0;
		for (unsigned int i = 0; i < cur_frame_data.fifoData.size(); ++i)
		{
			if ((i % 100)==0)
				printf("Processing fifo command %d of %d!\n", i, cur_frame_data.fifoData.size());

			const FifoFrameData &frame = fifo_data.frames[cur_frame];
			for (unsigned int update = 0; update < frame.memoryUpdates.size(); ++update)
			{
				if ((!last_pos || frame.memoryUpdates[update].fifoPosition > last_pos) && frame.memoryUpdates[update].fifoPosition <= i)
				{
//					PrepareMemoryLoad(frame.memoryUpdates[update].address, frame.memoryUpdates[update].dataSize);
					fseek(fifo_data.file, frame.memoryUpdates[update].dataOffset, SEEK_SET);
					fread(GetPointer(frame.memoryUpdates[update].address), frame.memoryUpdates[update].dataSize, 1, fifo_data.file);

					// DCFlushRange expects aligned addresses
					u32 off = frame.memoryUpdates[update].address % DEF_ALIGN;
					DCFlushRange(GetPointer(frame.memoryUpdates[update].address - off), frame.memoryUpdates[update].dataSize+off);
				}
			}
			last_pos = i;

			bool skip_stuff = false;
			static u32 efbcopy_target = 0;
			if (next_cmd_start != cur_analyzed_frame.cmd_starts.end() && *next_cmd_start == i &&
				cur_analyzed_frame.cmd_enabled[next_cmd_start-cur_analyzed_frame.cmd_starts.begin()])
			{
				if (cur_frame_data.fifoData[i] == 0x61) // load BP reg
				{
					// Patch texture addresses
					if ((cur_frame_data.fifoData[i+1] >= BPMEM_TX_SETIMAGE3 && cur_frame_data.fifoData[i+1] < BPMEM_TX_SETIMAGE3+4) ||
						(cur_frame_data.fifoData[i+1] >= BPMEM_TX_SETIMAGE3_4 && cur_frame_data.fifoData[i+1] < BPMEM_TX_SETIMAGE3_4+4))
					{
						u32 tempval = /*be32toh*/(*(u32*)&cur_frame_data.fifoData[i+1]);
						TexImage3* img = (TexImage3*)&tempval;
						u32 addr = img->image_base << 5; // TODO: Proper mask?
						u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
						img->image_base = new_addr >> 5;
						u32 new_value = /*h32tobe*/(tempval);

						wgPipe->U8 = 0x61;
						wgPipe->U32 = ((u32)cur_frame_data.fifoData[i+1]<<24)|(/*be32toh*/(new_value)&0xffffff);

						i += 4;
						skip_stuff = true;

						if (cur_frame_data.fifoData[i+1] >= BPMEM_TX_SETIMAGE3 && cur_frame_data.fifoData[i+1] < BPMEM_TX_SETIMAGE3+4)
							tex_addr[cur_frame_data.fifoData[i+1] - BPMEM_TX_SETIMAGE3] = new_addr;
						else
							tex_addr[4 + cur_frame_data.fifoData[i+1] - BPMEM_TX_SETIMAGE3_4] = new_addr;
					}
					else if (cur_frame_data.fifoData[i+1] == BPMEM_PRELOAD_ADDR)
					{
						// TODO
					}
					else if (cur_frame_data.fifoData[i+1] == BPMEM_LOADTLUT0)
					{
#if 1
						u32 tempval = /*be32toh*/(*(u32*)&cur_frame_data.fifoData[i+1]);
						u32 addr = tempval << 5; // TODO: Proper mask?
						u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
						u32 new_value = new_addr >> 5;

						wgPipe->U8 = cur_frame_data.fifoData[i];
						wgPipe->U32 = (BPMEM_LOADTLUT0<<24)|(/*be32toh*/(new_value)&0xffffff);

						i += 4;
						skip_stuff = true;
#endif
					}
					else if (cur_frame_data.fifoData[i+1] == BPMEM_EFB_ADDR)
					{
						u32 tempval = /*be32toh*/(*(u32*)&cur_frame_data.fifoData[i+1]);
						u32 addr = (tempval & 0xFFFFFF) << 5; // TODO
						efbcopy_target = addr;
					}
					else if (cur_frame_data.fifoData[i+1] == BPMEM_TRIGGER_EFB_COPY)
					{
						u32 tempval = /*be32toh*/(*(u32*)&cur_frame_data.fifoData[i+1]);
						UPE_Copy* copy = (UPE_Copy*)&tempval;
						if (!copy->copy_to_xfb)
						{
							bool update_textures = PrepareMemoryLoad(efbcopy_target, 640*480*4); // TODO: Size!!
							u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(efbcopy_target));
							u32 new_value = /*h32tobe*/((BPMEM_EFB_ADDR<<24) | (new_addr >> 5));

							// Update target address
							wgPipe->U8 = 0x61;
							wgPipe->U32 = (BPMEM_EFB_ADDR<<24)|(/*be32toh*/(new_value)&0xffffff);

							// Gotta fix texture offsets if memory map layout changed
							if (update_textures)
							{
								for (int k = 0; k < 8; ++k)
								{
									u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(tex_addr[k]));
									u32 new_value = /*h32tobe*/(new_addr>>5);

									wgPipe->U8 = 0x61;
									u32 reg = (k < 4) ? (BPMEM_TX_SETIMAGE3+k) : (BPMEM_TX_SETIMAGE3_4+(k-4));
									wgPipe->U32 = (reg<<24)|(/*be32toh*/(new_value)&0xffffff);
								}
							}
						}
					}
					else
					{
						wgPipe->U8 = 0x61;
						wgPipe->U8 = cur_frame_data.fifoData[i+1];
						wgPipe->U8 = cur_frame_data.fifoData[i+2];
						wgPipe->U8 = cur_frame_data.fifoData[i+3];
						wgPipe->U8 = cur_frame_data.fifoData[i+4];

						i += 4;
						skip_stuff = true;
					}
				}
				else if (cur_frame_data.fifoData[i] == GX_LOAD_CP_REG)
				{
					u8 cmd2 = cur_frame_data.fifoData[i+1];
					if ((cmd2 & 0xF0) == 0xA0)
					{
						u32 old_addr = *(u32*)&cur_frame_data.fifoData[i+2]; // TODO: Endiannes (only works on Wii)
						u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(old_addr));
						wgPipe->U8 = GX_LOAD_CP_REG;
						wgPipe->U8 = cur_frame_data.fifoData[i+1];
						wgPipe->U32 = new_addr;
						skip_stuff = true;
						i += 5;
					}

					u32 value = *(u32*)&cur_frame_data.fifoData[i+2]; // TODO: Endianness (only works on Wii)
					LoadCPReg(cmd2, value, cpmem);
				}
				else if (cur_frame_data.fifoData[i] == GX_LOAD_XF_REG)
				{
					// Load data directly instead of going through the loop again for no reason

					u32 cmd2 = *(u32*)&cur_frame_data.fifoData[i+1]; // TODO: Endianness (only works on Wii)
					u8 streamSize = ((cmd2 >> 16) & 15) + 1;

					wgPipe->U8 = cur_frame_data.fifoData[i];
					wgPipe->U32 = cmd2;
					for (int byte = 0; byte < streamSize * 4; ++byte)
						wgPipe->U8 = cur_frame_data.fifoData[i+5+byte];

					i += streamSize * 4 + 4;
					skip_stuff = true;
				}
				else if(cur_frame_data.fifoData[i] == GX_LOAD_INDX_A ||
						cur_frame_data.fifoData[i] == GX_LOAD_INDX_B ||
						cur_frame_data.fifoData[i] == GX_LOAD_INDX_C ||
						cur_frame_data.fifoData[i] == GX_LOAD_INDX_D)
				{
					wgPipe->U8 = cur_frame_data.fifoData[i];
					wgPipe->U8 = cur_frame_data.fifoData[i+1];
					wgPipe->U8 = cur_frame_data.fifoData[i+2];
					wgPipe->U8 = cur_frame_data.fifoData[i+3];
					wgPipe->U8 = cur_frame_data.fifoData[i+4];

					i += 4;
					skip_stuff = true;
				}
				else if (cur_frame_data.fifoData[i] & 0x80)
				{
					u32 vtxAttrGroup = cur_frame_data.fifoData[i] & GX_VAT_MASK;
					int vertexSize = CalculateVertexSize(vtxAttrGroup, cpmem);

					u16 streamSize = *(u16*)&cur_frame_data.fifoData[i+1]; // TODO: Endianness (only works on Wii)

					wgPipe->U8 = cur_frame_data.fifoData[i];
					wgPipe->U16 = streamSize;
					for (int byte = 0; byte < streamSize * vertexSize; ++byte)
						wgPipe->U8 = cur_frame_data.fifoData[i+3+byte];

					i += 2 + streamSize * vertexSize;
					skip_stuff = true;
				}
				++next_cmd_start;
			}
#if ENABLE_CONSOLE!=1
			if (!skip_stuff)
				wgPipe->U8 = cur_frame_data.fifoData[i];
#endif
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
			for (unsigned int i = 0; i < fifo_data.frames[0].fifoData.size(); ++i)
			{
				printf("%02x", fifo_data.frames[0].fifoData[i]);
				if (i == fifo_data.frames[0].fifoData.size()-5) printf("_");
//				if ((i % 4) == 3) printf(" ");
//				if ((i % 16) == 15) printf("\n");
				if ((i % 4) == 3) printf(" ");
				if ((i % 24) == 23) printf("\n");
			}
			fclose(fifo_data.file);
			exit(0);
		}

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	fclose(fifo_data.file);

	return 0;
}
