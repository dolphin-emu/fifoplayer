#include "DffFile.h"
#include "FifoDataFile.h"

void LoadDffData(const char* filename, FifoData& out)
{
	out.file = fopen(filename, "rb");
	if (!out.file)
		printf("Failed to open file!\n");
	else printf("File opened successfully!\n");

	DffFileHeader header;
	size_t numread = fread(&header, sizeof(DffFileHeader), 1, out.file);
	header.FixEndianness();

	if (header.fileId != 0x0d01f1f0 || header.min_loader_version > 2)
	{
		printf ("file ID or version don't match!\n");
	}
	out.version = header.file_version;

	printf ("Got %d frame%s\n", header.frameCount, (header.frameCount == 1) ? "" : "s");

	for (unsigned int i = 0; i < header.frameCount; ++i)
	{
		u64 frameOffset = header.frameListOffset + (i * sizeof(DffFrameInfo));
		DffFrameInfo srcFrame;

		fseek(out.file, frameOffset, SEEK_SET);
		fread(&srcFrame, sizeof(DffFrameInfo), 1, out.file);
		srcFrame.FixEndianness();

		out.frames.push_back(FifoFrameData());
		FifoFrameData& dstFrame = out.frames[i];

		// Version 1 did not support XFB copies.
		// To get it working at all the last 5 bytes are assumed
		// to be a EFB->XFB copy and replaced with manual XFB handling.
		// Thus they're not loaded into the actual fifo data.
		u32 used_fifo_data_size = (header.file_version < 2) ? (srcFrame.fifoDataSize-5) : srcFrame.fifoDataSize;
		dstFrame.fifoData.resize(used_fifo_data_size);
		fseek(out.file, srcFrame.fifoDataOffset, SEEK_SET);
		fread(&dstFrame.fifoData[0], used_fifo_data_size, 1, out.file);

		dstFrame.memoryUpdates.resize(srcFrame.numMemoryUpdates);
		for (unsigned int i = 0; i < srcFrame.numMemoryUpdates; ++i)
		{
			u64 updateOffset = srcFrame.memoryUpdatesOffset + (i * sizeof(DffMemoryUpdate));
			DffMemoryUpdate& srcUpdate = dstFrame.memoryUpdates[i];
			fseek(out.file, updateOffset, SEEK_SET);
			fread(&srcUpdate, sizeof(DffMemoryUpdate), 1, out.file);
			srcUpdate.FixEndianness();
		}

		if (header.file_version >= 2)
		{
			dstFrame.asyncEvents.resize(srcFrame.numAsyncEvents);
			for (unsigned int i = 0; i < srcFrame.numAsyncEvents; ++i)
			{
				u64 eventOffset = srcFrame.asyncEventsOffset + (i * sizeof(DffAsyncEvent));
				DffAsyncEvent& srcEvent = dstFrame.asyncEvents[i];
				fseek(out.file, eventOffset, SEEK_SET);
				fread(&srcEvent, sizeof(DffAsyncEvent), 1, out.file);
				srcEvent.FixEndianness();
			}
		}
	}

	// Save initial state
	u32 bp_size = header.bpMemSize;
	out.bpmem.resize(bp_size);
	fseek(out.file, header.bpMemOffset, SEEK_SET);
	fread(&out.bpmem[0], bp_size*4, 1, out.file);

	u32 cp_size = header.cpMemSize;
	out.cpmem.resize(cp_size);
	fseek(out.file, header.cpMemOffset, SEEK_SET);
	fread(&out.cpmem[0], cp_size*4, 1, out.file);

	u32 xf_size = header.xfMemSize;
	out.xfmem.resize(xf_size);
	fseek(out.file, header.xfMemOffset, SEEK_SET);
	fread(&out.xfmem[0], xf_size*4, 1, out.file);

	u32 xf_regs_size = header.xfRegsSize;
	out.xfregs.resize(xf_regs_size);
	fseek(out.file, header.xfRegsOffset, SEEK_SET);
	fread(&out.xfregs[0], xf_regs_size*4, 1, out.file);

	if (header.file_version >= 2)
	{
		u32 vi_size = header.viMemSize;
		out.vimem.resize(vi_size);
		fseek(out.file, header.viMemOffset, SEEK_SET);
		fread(&out.vimem[0], vi_size*2, 1, out.file);
	}
}
