#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <gccore.h>

#include <vector>
#include <map>

#include "memory_manager.h"

static std::map<u32, aligned_buf> memory_map; // map of memory chunks (indexed by starting address)


aligned_buf::aligned_buf() : buf(NULL), size(0), alignment(DEF_ALIGN)
{

}

aligned_buf::aligned_buf(int alignment) : buf(NULL), size(0), alignment(alignment)
{

}

aligned_buf::~aligned_buf()
{
	free(buf);
}

aligned_buf::aligned_buf(const aligned_buf& oth)
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

void aligned_buf::resize(int new_size)
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


bool IntersectsMemoryRange(u32 start1, u32 size1, u32 start2, u32 size2)
{
	return size1 && size2 && ((start1 >= start2 && start1 < start2 + size2) ||
			(start2 >= start1 && start2 < start1 + size1));
}

u32 FixupMemoryAddress(u32 addr)
{
	switch (addr >> 28)
	{
		case 0x0:
		case 0x8:
			addr &= 0x1FFFFFF; // RAM_MASK
			break;

		case 0x1:
		case 0x9:
		case 0xd:
			// TODO: Iff Wii
			addr &= 0x3FFFFFF; // EXRAM_MASK
			break;

		default:
			printf("CRITICAL: Unkown memory location %x!\n", addr);
			exit(0); // I'd rather exit than not noticing this kind of issue...
			break;
	}
	return addr;
}

// TODO: Needs to take care of alignment, too!
// Returns true if memory layout changed
bool PrepareMemoryLoad(u32 start_addr, u32 size)
{
	bool ret = false;

	start_addr = FixupMemoryAddress(start_addr);

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
	addr = FixupMemoryAddress(addr);

	for (auto it = memory_map.begin(); it != memory_map.end(); ++it)
		if (addr >= it->first && addr < it->first + it->second.size)
			return &it->second.buf[addr - it->first];

	return NULL;
}
