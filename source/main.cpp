#define ENABLE_CONSOLE 0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "memory_manager.h"

#include "VideoInterface.h"

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;


static u32 efbcopy_target = 0;

static u32 tex_addr[8] = {0};

static vu16* const _viReg = (u16*)0xCC002000;
using namespace VideoInterface;
void ApplyInitialState(const FifoData& fifo_data, u32* tex_addr, CPMemory& target_cpmem)
{
	const std::vector<u32>& bpmem = fifo_data.bpmem;
	const std::vector<u32>& cpmem = fifo_data.cpmem;
	const std::vector<u32>& xfmem = fifo_data.xfmem;
	const std::vector<u32>& xfregs = fifo_data.xfregs;
	const std::vector<u16>& vimem = fifo_data.vimem;

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

	for (unsigned int i = 0; i < fifo_data.vimem.size(); ++i)
	{
		u16 new_value = vimem[i];

		// Patch texture addresses
		if ((2*i >= VI_FB_LEFT_TOP_HI && 2*i < VI_FB_LEFT_TOP_HI+4) ||
			(2*i >= VI_FB_LEFT_BOTTOM_HI && 2*i < VI_FB_LEFT_BOTTOM_HI+4))
		{
			u32 tempval;
			if (2*i == VI_FB_LEFT_TOP_HI)
			{
				// also swapping the two u16 values
				tempval = ((u32)le16toh(vimem[VI_FB_LEFT_TOP_HI/2])) | ((u32)le16toh(vimem[VI_FB_LEFT_TOP_LO/2]) << 16);
			}
			else if (2*i == VI_FB_LEFT_BOTTOM_HI)
			{
				// also swapping the two u16 values
				tempval = ((u32)le16toh(vimem[VI_FB_LEFT_BOTTOM_HI/2])) | ((u32)le16toh(vimem[VI_FB_LEFT_BOTTOM_LO/2]) << 16);
			}
			UVIFBInfoRegister* reg = (UVIFBInfoRegister*)&tempval;
			u32 addr = (reg->POFF) ? (reg->FBB << 5) : reg->FBB;
			u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
			reg->FBB = (reg->POFF) ? (new_addr >> 5) : new_addr;

			printf("XFB %s at %x (redirected to %x)\n", (2*i==VI_FB_LEFT_TOP_HI) ? "top" : "bottom", addr, new_addr);

			u16 new_value_hi = h16tole(tempval >> 16);
			u16 new_value_lo = h16tole(tempval & 0xFFFF);

#if ENABLE_CONSOLE!=1
			// "raw" register poking broken for some reason, using the easy method for now...
			/*u32 level;
			_CPU_ISR_Disable(level);
			_viReg[i] = new_value_hi;
			_viReg[i+1] = new_value_lo;
			_CPU_ISR_Restore(level);*/
//			VIDEO_SetNextFramebuffer(GetPointer(new_addr));
			VIDEO_SetNextFramebuffer(GetPointer(efbcopy_target)); // and there go our haxx..
#endif

			++i;  // increase i by 2
			continue;
		}

#if ENABLE_CONSOLE!=1
		// TODO: Is this correct?
//		_viReg[i] = new_value;
#endif
	}

#if ENABLE_CONSOLE!=1
	#define MLoadCPReg(addr, val) { wgPipe->U8 = 0x08; wgPipe->U8 = addr; wgPipe->U32 = val; target_cpmem.LoadReg(addr, le32toh(cpmem[addr])); }

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
		// 0xA0 has the addresses of vertex arrays, which need to be relocated.
		u32 addr = le32toh(cpmem[0xa0 + i]);
		addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(addr));
		MLoadCPReg(0xa0 + i, addr);
		MLoadCPReg(0xb0 + i, le32toh(cpmem[0xb0 + i]));
	}
	#undef MLoadCPReg

	for (unsigned int i = 0; i < xfmem.size(); i += 16)
	{
		wgPipe->U8 = 0x10;
		wgPipe->U32 = 0xf0000 | (i&0xffff); // load 16*4 bytes at once
		for (int k = 0; k < 16; ++k)
		{
			wgPipe->U32 = le32toh(xfmem[i + k]);
		}
	}

	for (unsigned int i = 0; i < xfregs.size(); ++i)
	{
		wgPipe->U8 = 0x10;
		wgPipe->U32 = 0x1000 | (i&0x0fff);
		u32 val = xfregs[i];
		if (i == 5) val = 1;
		wgPipe->U32 = le32toh(xfregs[i]);
	}

	// Flush WGP
	for (int i = 0; i < 7; ++i)
		wgPipe->U32 = 0;
	wgPipe->U16 = 0;
	wgPipe->U8 = 0;
#endif
}

// Removes redundant data from a fifo log
void OptimizeFifoData(FifoData& fifo_data)
{
	for (auto frame : fifo_data.frames)
	{
//		for (auto byte : frame.)
	}
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
#if ENABLE_CONSOLE!=1
	frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode)); // TODO: Shouldn't require manual framebuffer management!
	frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(frameBuffer[fb]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if(rmode->viTVMode & VI_NON_INTERLACE)
		VIDEO_WaitVSync();
#endif

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
	ReadStreamedDff(client_socket, CheckIfHomePressed);

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
		CheckForNetworkEvents(server_socket, client_socket, fifo_data.frames, analyzed_frames);

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
					// TODO: Check for BPMEM_PRELOAD_MODE
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

						// Version 1 did not support EFB->XFB copies
						if (fifo_data.version >= 2 || !copy->copy_to_xfb)
						{
							bool update_textures = PrepareMemoryLoad(efbcopy_target, 640*480*4); // TODO: Size!!
							u32 new_addr = MEM_VIRTUAL_TO_PHYSICAL(GetPointer(efbcopy_target));
							u32 new_value = /*h32tobe*/((BPMEM_EFB_ADDR<<24) | (new_addr >> 5));

#if ENABLE_CONSOLE!=1
							// Update target address
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
#if ENABLE_CONSOLE!=1
						wgPipe->U8 = 0x61;
						wgPipe->U8 = cmd_data[1];
						wgPipe->U8 = cmd_data[2];
						wgPipe->U8 = cmd_data[3];
						wgPipe->U8 = cmd_data[4];
#endif
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

					cpmem.LoadReg(cmd2, value);
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
		if (fifo_data.version < 2)
		{
			// finish frame for legacy dff files
			//
			// Note that GX_CopyDisp(frameBuffer[fb],GX_TRUE) uses an internal state
			// which is out of sync with the dff_data, so we're manually writing
			// to the EFB copy registers instead.
			wgPipe->U8 = GX_LOAD_BP_REG;
			wgPipe->U32 = (BPMEM_EFB_ADDR << 24) | ((MEM_VIRTUAL_TO_PHYSICAL(frameBuffer[fb]) >> 5) & 0xFFFFFF);

			u32 temp;
			UPE_Copy& copy = *(UPE_Copy*)&temp;
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
		}
#endif
		VIDEO_Flush();
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
			fclose(fifo_data.file);
			exit(0);
		}

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	fclose(fifo_data.file);

	return 0;
}
