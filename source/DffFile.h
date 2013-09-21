#ifndef FIFOPLAYER_DFFFILE_H
#define FIFOPLAYER_DFFFILE_H

#include "CommonTypes.h"

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else // asumming libogc
#include <gctypes.h>
#endif

#if BYTE_ORDER==BIG_ENDIAN
static uint64_t le64toh(uint64_t val)
{
	return ((val&0xff)<<56)|((val&0xff00)<<40)|((val&0xff0000)<<24)|((val&0xff000000)<<8) |
		((val&0xff00000000)>>8)|((val&0xff0000000000)>>24)|((val&0xff000000000000)>>40)|((val&0xff00000000000000)>>56);
}

static uint32_t le32toh(uint32_t val)
{
	return ((val&0xff)<<24)|((val&0xff00)<<8)|((val&0xff0000)>>8)|((val&0xff000000)>>24);
}

static uint32_t h32tole(uint32_t val)
{
	return le32toh(val);
}

static uint16_t le16toh(uint16_t val)
{
	return ((val&0xff)<<8)|((val&0xff00)>>8);
}

static uint32_t h16tole(uint32_t val)
{
	return le16toh(val);
}

static uint64_t be64toh(uint64_t val)
{
	return val;
}

static uint32_t be32toh(uint32_t val)
{
	return val;
}

static uint16_t be16toh(uint16_t val)
{
	return val;
}
#elif BYTE_ORDER==LITTLE_ENDIAN
// endian.h should have taken care of defining these...
#endif


#pragma pack(push, 4)

union DffFileHeader {
	struct {
		// Version 1 data
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

		// Version 2 data
		u32 viMemOffset;
		u32 viMemSize;
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
		viMemOffset = le32toh(viMemOffset);
		viMemSize = le32toh(viMemSize);
	}
};

union DffFrameInfo
{
	struct
	{
		// Version 1 data
		u64 fifoDataOffset;
		u32 fifoDataSize;
		u32 fifoStart;
		u32 fifoEnd;
		u64 memoryUpdatesOffset;
		u32 numMemoryUpdates;

		// Version 2 data
		u64 asyncEventsOffset;
		u32 numAsyncEvents;
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
		asyncEventsOffset = le64toh(asyncEventsOffset);
		numAsyncEvents = le32toh(numAsyncEvents);
	}
};

struct DffMemoryUpdate
{
	enum Type
	{
		TEXTURE_MAP = 0x01,
		XF_DATA = 0x02,
		VERTEX_STREAM = 0x04,
		TMEM = 0x08,
	};

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

struct DffAsyncEvent
{
	enum Type
	{
		VI_WRITE16 = 0x00,
		VI_WRITE32 = 0x01,
	};

    u32 fifoPosition;
    u8 type;

	union
	{
		struct
		{
			u32 addr;
			u16 data;
		} vi_write16;

		struct
		{
			u32 addr;
			u32 data;
		} vi_write32;

		u32 data[2];
	};

	void FixEndianness()
	{
		fifoPosition = le32toh(fifoPosition);

		if (type == VI_WRITE16)
		{
			vi_write16.addr = le32toh(vi_write16.addr);
			vi_write16.data = le16toh(vi_write16.data);
		}
		else if (type == VI_WRITE32)
		{
			vi_write32.addr = le32toh(vi_write32.addr);
			vi_write32.data = le32toh(vi_write32.data);
		}
	}
};


#pragma pack(pop)

#endif  // FIFOPLAYER_DFFFILE_H
