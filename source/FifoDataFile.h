// Data structures used when loading dff files

#ifndef FIFOPLAYER_FIFODATAFILE_H
#define FIFOPLAYER_FIFODATAFILE_H

#include "CommonTypes.h"
#include <vector>
#include <stdio.h>

//struct DffMemoryUpdate;
#include "DffFile.h"

struct FifoFrameData
{
	std::vector<u8> fifoData;
	u32 fifoStart;
	u32 fifoEnd;

	// Sorted by position - TODO: Make this a map instead?
	std::vector<DffMemoryUpdate> memoryUpdates;
};

struct FifoData
{
	FILE* file;

	std::vector<FifoFrameData> frames;

	std::vector<u32> bpmem;
	std::vector<u32> cpmem;
	std::vector<u32> xfmem;
	std::vector<u32> xfregs;
};

void LoadDffData(const char* filename, FifoData& out);

#endif  // FIFOPLAYER_FIFODATAFILE_H
