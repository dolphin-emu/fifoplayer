#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>
#include <stdint.h>
#include <iostream>
#include <machine/endian.h>

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t u8;

#include "data.h"

std::map<u32, std::vector<u8> > memory_map; // map of memory chunks (indexed by starting address)

bool IntersectsMemoryRange(u32 start1, u32 size1, u32 start2, u32 size2)
{
	return size1 && size2 && ((start1 >= start2 && start1 < start2 + size2) ||
			(start2 >= start1 && start2 < start1 + size1));
}

void PrepareMemoryLoad(u32 start_addr, u32 size)
{
	std::vector<u32> affected_elements;
	u32 new_start_addr = start_addr;
	u32 new_end_addr = start_addr + size - 1;

	// Find overlaps with existing memory chunks
	for (auto it = memory_map.begin(); it != memory_map.end(); ++it)
	{
		if (IntersectsMemoryRange(it->first, it->second.size(), start_addr, size))
		{
			affected_elements.push_back(it->first);
			if (it->first < new_start_addr)
				new_start_addr = it->first;
			if (it->first + it->second.size() > new_end_addr + 1)
				new_end_addr = it->first + it->second.size() - 1;
		}
	}

	std::vector<u8>& new_memchunk = memory_map[new_start_addr]; // creates a new vector or uses the existing one
	u32 new_size = new_end_addr - new_start_addr + 1;

	// if the new memory range is inside an existing chunk, there's nothing to do
	if (new_memchunk.size() == new_size)
		return;

	// resize chunk to required size, move old content to it, replace old arrays with new one
	// NOTE: can't do reserve here because not the whole memory might be covered by existing memory chunks
	new_memchunk.resize(new_size);
	while (!affected_elements.empty())
	{
		u32 addr = affected_elements.back();

		// first chunk is already in new_memchunk
		if (addr != new_start_addr)
		{
			std::vector<u8>& src = memory_map[addr];
			memcpy(&new_memchunk[addr - new_start_addr], &src[0], src.size());
			memory_map.erase(addr);
		}
		affected_elements.pop_back();
	}

	// TODO: Handle critical case where memory allocation fails!
}

// Must have been reserved via PrepareMemoryLoad first
u8* GetPointer(u32 addr)
{
	for (auto it = memory_map.begin(); it != memory_map.end(); ++it)
		if (addr >= it->first && addr < it->first + it->second.size())
			return &it->second[addr - it->first];

	return NULL;
}

#if BYTE_ORDER==BIG_ENDIAN
uint64_t le64toh(uint64_t val)
{
	return ((val&0xff)<<56)|((val&0xff00)<<40)|((val&0xff0000)<<24)|((val&0xff000000)<<8) |
		((val&0xff00000000)>>8)|((val&0xff0000000000)>>24)|((val&0xff000000000000)>>40)|((val&0xff00000000000000)>>56);
}

uint32_t le32toh(uint32_t val)
{
	return ((val&0xff)<<24)|((val&0xff00)<<8)|((val&0xff0000)>>8)|((val&0xff000000)>>24);
}

uint16_t le16toh(uint16_t val)
{
	return ((val&0xff)<<8)|((val&0xff00)>>8);
}


#elif BYTE_ORDER==LITTLE_ENDIAN
#error other stuff
#endif

#pragma pack(push, 4)

union DffFileHeader {
	struct {
		u32 fileId;
		u32 file_version;
		u32 min_loader_version;
		u64 bpMemOffset;
		u32 bpMemSize;
		u64 cpMemOffset;
		u32 cpMemSize;
		u64 xfMemOffset;
		u32 xfMemSize;
		u64 xfRegsOffset;
		u32 xfRegsSize;
		u64 frameListOffset;
		u32 frameCount;
		u32 flags;
	};
	u32 rawData[32];

	void FixEndianness()
	{
		fileId = le32toh(fileId);
		file_version = le32toh(file_version);
		min_loader_version = le32toh(min_loader_version);
		bpMemOffset = le64toh(bpMemOffset);
		bpMemSize = le32toh(bpMemSize);
		cpMemOffset = le64toh(cpMemOffset);
		cpMemSize = le32toh(cpMemSize);
		xfMemOffset = le64toh(xfMemOffset);
		xfMemSize = le32toh(xfMemSize);
		xfRegsOffset = le64toh(xfRegsOffset);
		xfRegsSize = le32toh(xfRegsSize);
		frameListOffset = le64toh(frameListOffset);
		frameCount = le32toh(frameCount);
		flags = le32toh(flags);
	}
};

union DffFrameInfo
{
	struct
	{
		u64 fifoDataOffset;
		u32 fifoDataSize;
		u32 fifoStart;
		u32 fifoEnd;
		u64 memoryUpdatesOffset;
		u32 numMemoryUpdates;
	};
	u32 rawData[16];

	void FixEndianness()
	{
		fifoDataOffset = le64toh(fifoDataOffset);
		fifoDataSize = le32toh(fifoDataSize);
		fifoStart = le32toh(fifoStart);
		fifoEnd = le32toh(fifoEnd);
		memoryUpdatesOffset = le64toh(memoryUpdatesOffset);
		numMemoryUpdates = le32toh(numMemoryUpdates);
	}
};

struct DffMemoryUpdate
{
	u32 fifoPosition;
	u32 address;
	u64 dataOffset;
	u32 dataSize;
	u8 type;

	void FixEndianness()
	{
		fifoPosition = le32toh(fifoPosition);
		address = le32toh(address);
		dataOffset = le64toh(dataOffset);
		dataSize = le32toh(dataSize);
	}
};
#include "stdio.h"

#define BPMEM_TRIGGER_EFB_COPY 0x52
#define BPMEM_CLEARBBOX1       0x55
#define BPMEM_CLEARBBOX2       0x56
#define BPMEM_CLEAR_PIXEL_PERF 0x57
#define BPMEM_SETDRAWDONE      0x45
#define BPMEM_PE_TOKEN_ID      0x47
#define BPMEM_PE_TOKEN_INT_ID  0x48
#define BPMEM_PRELOAD_MODE     0x63
#define BPMEM_LOADTLUT0        0x64
#define BPMEM_LOADTLUT1        0x65
#define BPMEM_TEXINVALIDATE    0x66


#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

void LoadDffData(u8* data, unsigned int& size)
{
	DffFileHeader header;
	memcpy(&header,  &dff_data[0], sizeof(DffFileHeader));
	header.FixEndianness();

	if (header.fileId != 0x0d01f1f0 || header.min_loader_version > 1)
	{
		printf ("file ID or version don't match!\n");
	}
	printf ("Got %d frame%s\n", header.frameCount, (header.frameCount == 1) ? "" : "s");

	for (unsigned int i = 0;i < header.frameCount; ++i)
	{
		u64 frameOffset = header.frameListOffset + (i * sizeof(DffFrameInfo));
		DffFrameInfo srcFrame;
		memcpy(&srcFrame, &dff_data[frameOffset], sizeof(DffFrameInfo));
		srcFrame.FixEndianness();

		printf("Frame %d got %d bytes of data (Start: 0x%#x, End: 0x%#x)\n", i, srcFrame.fifoDataSize, srcFrame.fifoStart, srcFrame.fifoEnd);
		// Skipping last 5 bytes, which are assumed to be a CopyDisp call for the XFB copy
		memcpy (data, &dff_data[srcFrame.fifoDataOffset], srcFrame.fifoDataSize-5);
		size = srcFrame.fifoDataSize-5;

		// Apply initial state
		u32 bp_size = header.bpMemSize;
		u32* bp_ptr = (u32*)&dff_data[header.bpMemOffset];
		printf("Reading %d BP registers\n", bp_size);
		for (unsigned int i = 0; i < bp_size; ++i)
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

			wgPipe->U8 = 0x61;
			wgPipe->U32 = (i<<24)|(le32toh(bp_ptr[i])&0xffffff);
		}

		#define LoadCPReg(addr, val) { wgPipe->U8 = 0x08; wgPipe->U8 = addr; wgPipe->U32 = val; }

		u32* regs = (u32*)&dff_data[header.cpMemOffset];
		printf("Reading CP memory\n");

		LoadCPReg(0x30, le32toh(regs[0x30]));
		LoadCPReg(0x40, le32toh(regs[0x40]));
		LoadCPReg(0x50, le32toh(regs[0x50]));
		LoadCPReg(0x60, le32toh(regs[0x60]));

		for (int i = 0; i < 8; ++i)
		{
			LoadCPReg(0x70 + i, le32toh(regs[0x70 + i]));
			LoadCPReg(0x80 + i, le32toh(regs[0x80 + i]));
			LoadCPReg(0x90 + i, le32toh(regs[0x90 + i]));
		}

		for (int i = 0; i < 16; ++i)
		{
			LoadCPReg(0xa0 + i, le32toh(regs[0xa0 + i]));
			LoadCPReg(0xb0 + i, le32toh(regs[0xb0 + i]));
		}

		u32 xf_size = header.xfMemSize;
		u32* xf_ptr = (u32*)&dff_data[header.xfMemOffset];
		printf("Reading %d parts of XF memory\n", xf_size);
		for (unsigned int i = 0; i < xf_size; i += 16)
		{
			wgPipe->U8 = 0x10;
			wgPipe->U32 = 0xf0000 | (i&0xffff); // load 16*4 bytes
			for (int k = 0; k < 16; ++k)
				wgPipe->U32 = le32toh(xf_ptr[i + k]);
		}

		u32 xf_regs_size = header.xfRegsSize;
		u32* xf_regs_ptr = (u32*)&dff_data[header.xfRegsOffset];
		printf("Reading %d XF registers\n", xf_regs_size);
		for (unsigned int i = 0x0; i < xf_regs_size; ++i)
		{
			wgPipe->U8 = 0x10;
			wgPipe->U32 = 0x1000 | (i&0x0fff);
			wgPipe->U32 = le32toh(xf_regs_ptr[i]);
			u32 val = xf_regs_ptr[i];
			val = le32toh(val);
			printf("reg value: %f\n", *(f32*)&val);
		}

		// Flush WGP
		for (int i = 0; i < 7; ++i)
			wgPipe->U32 = 0;
		wgPipe->U16 = 0;
		wgPipe->U8 = 0;

	}
}

#pragma pack(pop)

#define DEFAULT_FIFO_SIZE   (256*1024)
#define ENABLE_CONSOLE 0
static void *frameBuffer[2] = { NULL, NULL};
GXRModeObj *rmode;

u32 fb = 0;
u32 first_frame = 1;

#include <unistd.h>

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
}

#include "mygx.h"

int main()
{
	Init();

	u8 data[512];
	unsigned int idx = 0;
	LoadDffData((u8*)&data[0], idx);

	for (unsigned int i = 0; i < idx; ++i)
	{
		printf("%02x", data[i]);
		if (i == idx-5) printf("_");
		if ((i % 4) == 3) printf(" ");
		if ((i % 16) == 15) printf("\n");
	}

	bool processing = true;
	int first_frame = 0;
	int last_frame = 0;
	int cur_frame = first_frame;
	while (processing)
	{
#if ENABLE_CONSOLE!=1
		for (unsigned int i = 0; i < idx; ++i)
			wgPipe->U8 = data[i];
#endif

		// for (element = frame_elements[cur_frame])
		{
			// switch (element.type)
			{
				// case LOAD_REG:
					// if (is_bp_reg)
					{
						// if (teximage3)
						{
							// TODO: Decode addr
							// GetPointer(addr);
							// TODO: Encode addr
						}
						// if (trigger_efbcopy)
						{
							// PrepareMemoryLoad(dest_addr, dest_size);
							// TODO: Decode addr
							// GetPointer(addr);
							// TODO: Encode addr
						}
					}
					// break;
				// case LOAD_MEM:
					// PrepareMemoryLoad(start_addr, size);
					// memcpy (GetPointer(element.start_addr), element.data, element.size);
					// break;
			}
		}

		// finish frame...
#if ENABLE_CONSOLE!=1
		GX_CopyDisp(frameBuffer[fb],GX_TRUE);

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

		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
			processing = false;

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	return 0;
}
