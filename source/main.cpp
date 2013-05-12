#include <stdlib.h>
#include <string.h>
#include <map>
#include <vector>

typedef unsigned int u32;
typedef unsigned char u8;

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

int main()
{
	// TODO: Setup GX state

	bool processing = true;
	int first_frame = 0;
	int last_frame = 0;
	int cur_frame = first_frame;
	while (processing)
	{
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

		// reset GX state
		// draw menu
		// restore GX state

		// input checking
		// A = select menu point
		// B = menu back
		// plus = pause
		// minus = hide menu
		// home = stop
		// if (stop)
			processing = false;

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	return 0;
}
