// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef FIFOPLAYER_FIFOANALYZER_H
#define FIFOPLAYER_FIFOANALYZER_H

#include "CommonTypes.h"
#include "VertexLoader.h"
#include "VertexLoader_Position.h"
#include "VertexLoader_Normal.h"
#include "VertexLoader_TextCoord.h"
#include "OpcodeDecoding.h"
#include "FifoDataFile.h"
#include "BPMemory.h"

#pragma pack(push, 4)
struct CPMemory
{
	TVtxDesc vtxDesc;
	VAT vtxAttr[8];
	u32 arrayBases[16];
	u32 arrayStrides[16];

	void LoadReg(u32 subCmd, u32 value)
	{
		switch (subCmd & 0xF0)
		{
		case 0x50:
			vtxDesc.Hex &= ~0x1FFFF;  // keep the Upper bits
			vtxDesc.Hex |= value; // !TODO!: Should mask this with 0x1FFFF
			break;

		case 0x60:
			vtxDesc.Hex &= 0x1FFFF;  // keep the lower 17Bits
			vtxDesc.Hex |= (u64)value * 131072;
			break;

		case 0x70:
			vtxAttr[subCmd & 7].g0.Hex = value;
			break;

		case 0x80:
			vtxAttr[subCmd & 7].g1.Hex = value;
			break;

		case 0x90:
			vtxAttr[subCmd & 7].g2.Hex = value;
			break;

		case 0xA0:
			arrayBases[subCmd & 0xF] = value;
			break;

		case 0xB0:
			arrayStrides[subCmd & 0xF] = value & 0xFF;
			break;
		}
	}
};

#pragma pack(pop)

static u8 ReadFifo8(u8 *&data)
{
	u8 value = data[0];
	data += 1;
	return value;
}

static u16 ReadFifo16(u8 *&data)
{
	u16 value = be16toh(*(u16*)data);
	data += 2;
	return value;
}

static u32 ReadFifo32(u8 *&data)
{
	u32 value = be32toh(*(u32*)data);
	data += 4;
	return value;
}

static void CalculateVertexElementSizes(int sizes[], int vatIndex, const CPMemory &cpMem)
{
	const TVtxDesc &vtxDesc = cpMem.vtxDesc;
	const VAT &vtxAttr = cpMem.vtxAttr[vatIndex];

	// Colors
	const u32 colDesc[2] = {vtxDesc.Color0, vtxDesc.Color1};
	const u32 colComp[2] = {vtxAttr.g0.Color0Comp, vtxAttr.g0.Color1Comp};

	const u32 tcElements[8] =
	{
		vtxAttr.g0.Tex0CoordElements, vtxAttr.g1.Tex1CoordElements, vtxAttr.g1.Tex2CoordElements,
		vtxAttr.g1.Tex3CoordElements, vtxAttr.g1.Tex4CoordElements, vtxAttr.g2.Tex5CoordElements,
		vtxAttr.g2.Tex6CoordElements, vtxAttr.g2.Tex7CoordElements
	};

	const u32 tcFormat[8] =
	{
		vtxAttr.g0.Tex0CoordFormat, vtxAttr.g1.Tex1CoordFormat, vtxAttr.g1.Tex2CoordFormat,
		vtxAttr.g1.Tex3CoordFormat, vtxAttr.g1.Tex4CoordFormat, vtxAttr.g2.Tex5CoordFormat,
		vtxAttr.g2.Tex6CoordFormat, vtxAttr.g2.Tex7CoordFormat
	};

	// Add position and texture matrix indices
	u64 vtxDescHex = cpMem.vtxDesc.Hex;
	for (int i = 0; i < 9; ++i)
	{
		sizes[i] = vtxDescHex & 1;
		vtxDescHex >>= 1;
	}

	// Position
	sizes[9] = VertexLoader_Position::GetSize(vtxDesc.Position, vtxAttr.g0.PosFormat, vtxAttr.g0.PosElements);

	// Normals
	if (vtxDesc.Normal != NOT_PRESENT)
	{
		sizes[10] = VertexLoader_Normal::GetSize(vtxDesc.Normal, vtxAttr.g0.NormalFormat, vtxAttr.g0.NormalElements, vtxAttr.g0.NormalIndex3);
	}
	else
	{
		sizes[10] = 0;
	}

	// Colors
	for (int i = 0; i < 2; i++)
	{
		int size = 0;

		switch (colDesc[i])
		{
		case NOT_PRESENT:
			break;
		case DIRECT:
			switch (colComp[i])
			{
			case FORMAT_16B_565:	size = 2; break;
			case FORMAT_24B_888:	size = 3; break;
			case FORMAT_32B_888x:	size = 4; break;
			case FORMAT_16B_4444:	size = 2; break;
			case FORMAT_24B_6666:	size = 3; break;
			case FORMAT_32B_8888:	size = 4; break;
			default: /*_assert_(0);*/ break;
			}
			break;
		case INDEX8:
			size = 1;
			break;
		case INDEX16:
			size = 2;
			break;
		}

		sizes[11 + i] = size;
	}

	// Texture coordinates
	vtxDescHex = vtxDesc.Hex >> 17;
	for (int i = 0; i < 8; i++)
	{
		sizes[13 + i] = VertexLoader_TextCoord::GetSize(vtxDescHex & 3, tcFormat[i], tcElements[i]);
		vtxDescHex >>= 2;
	}
}

static u32 CalculateVertexSize(int vatIndex, const CPMemory &cpMem)
{
	u32 vertexSize = 0;

	int sizes[21];
	CalculateVertexElementSizes(sizes, vatIndex, cpMem);

	for (int i = 0; i < 21; ++i)
		vertexSize += sizes[i];

	return vertexSize;
}

struct AnalyzedObject
{
	AnalyzedObject(u32 start, u32 end, u32 last_cmd_byte) : start(start), end(end), last_cmd_byte(last_cmd_byte) {}

	u32 start; // Address of first command in a polygon rendering series
	u32 end;  // Address of first command after rendering polygons (commands can still be after this!)
	u32 last_cmd_byte;  // Address of last command byte

	std::vector<u32> cmd_starts;
	std::vector<bool> cmd_enabled;
};

struct AnalyzedFrameInfo
{
	std::vector<AnalyzedObject> objects;

//	std::vector<MemoryUpdate> memory_updates;
};

class FifoDataAnalyzer
{
public:
	// TODO: Does this even still need to be non-static?
	void AnalyzeFrames(FifoData& data, std::vector<AnalyzedFrameInfo>& frame_info)
	{
		// TODO: Load BP mem

		u32 *cpMem = &data.cpmem[0];
		m_cpmem.LoadReg(0x50, le32toh(cpMem[0x50]));
		m_cpmem.LoadReg(0x60, le32toh(cpMem[0x60]));

		for (int i = 0; i < 8; ++i)
		{
			m_cpmem.LoadReg(0x70 + i, le32toh(cpMem[0x70 + i]));
			m_cpmem.LoadReg(0x80 + i, le32toh(cpMem[0x80 + i]));
			m_cpmem.LoadReg(0x90 + i, le32toh(cpMem[0x90 + i]));
		}

		frame_info.clear();
		frame_info.resize(data.frames.size());

		m_drawingObject = false;

		for (unsigned int frame_idx = 0; frame_idx < data.frames.size(); ++frame_idx)
		{
			FifoFrameData& src_frame = data.frames[frame_idx];
			AnalyzedFrameInfo& dst_frame = frame_info[frame_idx];

			u32 cmd_start = 0;
			dst_frame.objects.push_back(AnalyzedObject(0, 0, 0)); // Add an empty object in case the current frame begins with register changes
			AnalyzedObject* cur_object = &dst_frame.objects.back(); // TODO: Ugly

			while (cmd_start < src_frame.fifoData.size())
			{
				bool was_drawing = m_drawingObject;
				bool unused;
				u32 cmd_size = DecodeCommandLegacy(&src_frame.fifoData[cmd_start], m_drawingObject, unused, m_cpmem);

				// TODO: Check that cmd_size != 0

				if (was_drawing != m_drawingObject)
				{
					if (m_drawingObject)
					{
						dst_frame.objects.push_back(AnalyzedObject(cmd_start, cmd_start, cmd_start));
						cur_object = &dst_frame.objects.back();
					}
					else
					{
						// TODO: Should make sure that the first command is always a draw command...
						cur_object->end = cmd_start;
					}
				}

				cur_object->cmd_starts.push_back(cmd_start);
				cur_object->cmd_enabled.push_back(true);
				cur_object->last_cmd_byte = cmd_start + cmd_size - 1;

				cmd_start += cmd_size;
			}
			if (m_drawingObject)
				dst_frame.objects.back().end = cmd_start;
		}
	}

	static std::vector<FifoFrameData> OptimizeFifoData(FifoData& data) // TODO: data should be const
	{
		CPMemory cpmem;
		u32 *src_cpmem_u32 = &data.cpmem[0];
		cpmem.LoadReg(0x50, le32toh(src_cpmem_u32[0x50]));
		cpmem.LoadReg(0x60, le32toh(src_cpmem_u32[0x60]));

		for (int i = 0; i < 8; ++i)
		{
			cpmem.LoadReg(0x70 + i, le32toh(src_cpmem_u32[0x70 + i]));
			cpmem.LoadReg(0x80 + i, le32toh(src_cpmem_u32[0x80 + i]));
			cpmem.LoadReg(0x90 + i, le32toh(src_cpmem_u32[0x90 + i]));
		}

		std::vector<FifoFrameData> optimized_frames;

		CPMemory old_cpmem = cpmem;

		bool is_drawing = false;
		for (auto frame : data.frames)
		{
			FifoFrameData optimized_frame;
			optimized_frame.fifoData.reserve(frame.fifoData.size());
			optimized_frame.fifoStart = 0; // These aren't used anyway
			optimized_frame.fifoEnd = 0;

			u32 cmd_start = 0;
			u32 old_start = cmd_start;
			while (cmd_start < frame.fifoData.size())
			{
				bool was_drawing = is_drawing;
				bool is_nontrivial_command = false;
				u32 cmd_size = DecodeCommandLegacy(&frame.fifoData[cmd_start], is_drawing, is_nontrivial_command, cpmem);

				// Found a nontrivial command
				// Before writing it, flush all state changes in the optimized
				// fifo data stream. Then, make sure to also apply all memory
				// updates in the affected range.
				is_nontrivial_command = true; // debugging!
				u32 optimized_start = optimized_frame.fifoData.size();
				if (is_nontrivial_command)
				{
#define SetCPReg(reg, val, oldval) \
		if((val) != (oldval)) \
		{\
			optimized_frame.fifoData.push_back(GX_LOAD_CP_REG);\
			optimized_frame.fifoData.push_back(reg);\
			optimized_frame.fifoData.push_back(1);\
			optimized_frame.fifoData.push_back(2);\
			optimized_frame.fifoData.push_back(3);\
			optimized_frame.fifoData.push_back(4);\
			u32* my_ptr = (u32*)&*(optimized_frame.fifoData.end()-4);\
			*my_ptr = htobe32((val)); \
		}
					SetCPReg(0x50, cpmem.vtxDesc.Hex&0x1FFFFull, old_cpmem.vtxDesc.Hex&0x1FFFFull);
					SetCPReg(0x60, ((u64)cpmem.vtxDesc.Hex&~0x1FFFFull)>>17, ((u64)old_cpmem.vtxDesc.Hex&~0x1FFFFull)>>17);
					for (int i = 0; i < 8; ++i)
					{
						SetCPReg(0x70+i, cpmem.vtxAttr[i].g0.Hex, old_cpmem.vtxAttr[i].g0.Hex);
						SetCPReg(0x80+i, cpmem.vtxAttr[i].g1.Hex, old_cpmem.vtxAttr[i].g1.Hex);
						SetCPReg(0x90+i, cpmem.vtxAttr[i].g2.Hex, old_cpmem.vtxAttr[i].g2.Hex);
					}
					for (int i = 0; i < 16; ++i)
					{
						SetCPReg(0xA0+i, cpmem.arrayBases[i], old_cpmem.arrayBases[i]);
						SetCPReg(0xB0+i, cpmem.arrayStrides[i], old_cpmem.arrayStrides[i]);
					}
					old_cpmem = cpmem;

					// TODO: Write all other unwritten data

					for (auto memory_update : frame.memoryUpdates)
					{
						// TODO: Check if these ranges are okay
						if (memory_update.fifoPosition >= old_start &&
							memory_update.fifoPosition < cmd_start + cmd_size)
						{
							DffMemoryUpdate optimized_memory_update = memory_update;
							optimized_memory_update.dataOffset = optimized_frame.fifoData.size();
							optimized_frame.memoryUpdates.push_back(optimized_memory_update);
						}
					}

					for (auto async_event : frame.asyncEvents)
					{
						// TODO: Check if these ranges are okay
						if (async_event.fifoPosition >= old_start &&
							async_event.fifoPosition < cmd_start + cmd_size)
						{
							DffAsyncEvent optimized_async_event = async_event;
							optimized_async_event.fifoPosition = optimized_frame.fifoData.size();
							optimized_frame.asyncEvents.push_back(optimized_async_event);
						}
					}

//					if(frame.fifoData[old_start] == GX_LOAD_CP_REG){
					printf("optimized: ");
					for (unsigned int i = optimized_start; i < optimized_frame.fifoData.size(); ++i)
					{
						printf("%02x ", optimized_frame.fifoData[i]);
					}
					printf("\n");
					printf("regular:   ");
					for (unsigned int i = old_start; i < cmd_start+cmd_size; ++i)
					{
						printf("%02x ", frame.fifoData[i]);
					}
					printf("\n\n");
//					}

					old_start = cmd_start+cmd_size;
				}
				if (frame.fifoData[cmd_start] != GX_LOAD_CP_REG) // TODO: We are not catching ALL kinds of CP writes... not sure if we should!
				{
					// Make sure to also write all commands which we don't handle, yet...
					for (u8* byte = &frame.fifoData[cmd_start]; byte != &frame.fifoData[cmd_start+cmd_size]; ++byte)
						optimized_frame.fifoData.push_back(*byte);
				}

				cmd_start += cmd_size;
			}

			// TODO: Write unflushed stuff!

			optimized_frames.push_back(optimized_frame);
		}
		return optimized_frames;
	}

	// Returns amount of bytes read
	// drawing_object will be "true" if the command was a drawing command, otherwise "false".
	// nontrivial_command will be set to true if the command triggers events other than plain configuration changes.
	static u32 DecodeCommand(u8* data, bool& drawing_object, bool &nontrivial_command, const CPMemory& cpmem)
	{
		u8* data_start = data;

		u8 cmd = ReadFifo8(data);

		nontrivial_command = false;

		switch (cmd)
		{
			case GX_NOP:
			case 0x44:
			case GX_CMD_INVL_VC:
				break;

			case GX_LOAD_CP_REG:
			{
				drawing_object = false;

				u32 cmd2 = ReadFifo8(data);
				u32 value = ReadFifo32(data);
				break;
			}

			case GX_LOAD_XF_REG:
			{
				drawing_object = false;

				u32 cmd2 = ReadFifo32(data);
				u8 stream_size = ((cmd2 >> 16) & 0xf) + 1; // TODO: Check if this works!

				data += stream_size * 4;
				break;
			}

			case GX_LOAD_INDX_A:
			case GX_LOAD_INDX_B:
			case GX_LOAD_INDX_C:
			case GX_LOAD_INDX_D:
				drawing_object = false;
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
				drawing_object = false;

				u32 cmd2 = ReadFifo32(data);
				u8 regid = cmd2>>24;

				// TODO: Might want to do some stuff here...

				if ((regid == BPMEM_TRIGGER_EFB_COPY
					|| regid == BPMEM_CLEARBBOX1
					|| regid == BPMEM_CLEARBBOX2
					|| regid == BPMEM_SETDRAWDONE
					|| regid == BPMEM_PE_TOKEN_ID // TODO: Sure that we want to skip this one?
					|| regid == BPMEM_PE_TOKEN_INT_ID
					|| regid == BPMEM_LOADTLUT0
					|| regid == BPMEM_LOADTLUT1
					|| regid == BPMEM_TEXINVALIDATE
					|| regid == BPMEM_PRELOAD_MODE
					|| regid == BPMEM_CLEAR_PIXEL_PERF))
					nontrivial_command = true;
				
				break;
			}

			default:
				if (cmd & 0x80)
				{
					nontrivial_command = true;
					drawing_object = true;
					u32 vtxAttrGroup = cmd & GX_VAT_MASK;
					int vertex_size = CalculateVertexSize(vtxAttrGroup, cpmem);

					u16 stream_size = ReadFifo16(data);
					data += stream_size * vertex_size;
				}
				else
				{
					printf("Invalid fifo command 0x%x\n", cmd);
//					sleep(1);
				}
				break;
		}
		return data - data_start;
	}

	static u32 DecodeCommandLegacy(u8* data, bool& drawing_object, bool& nontrivial_command, CPMemory& cpmem)
	{
		u8* data_start = data;

		u8 cmd = ReadFifo8(data);

		switch (cmd)
		{
			case GX_NOP:
			case 0x44:
			case GX_CMD_INVL_VC:
				break;

			case GX_LOAD_CP_REG:
			{
				drawing_object = false;

				u32 cmd2 = ReadFifo8(data);
				u32 value = ReadFifo32(data);
				cpmem.LoadReg(cmd2, value);
				break;
			}

			case GX_LOAD_XF_REG:
			{
				drawing_object = false;

				u32 cmd2 = ReadFifo32(data);
				u8 stream_size = ((cmd2 >> 16) & 0xf) + 1; // TODO: Check if this works!

				data += stream_size * 4;
				break;
			}

			case GX_LOAD_INDX_A:
			case GX_LOAD_INDX_B:
			case GX_LOAD_INDX_C:
			case GX_LOAD_INDX_D:
				drawing_object = false;
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
				drawing_object = false;

				u32 cmd2 = ReadFifo32(data);
				u8 regid = cmd2>>24;

//				printf("BP: %02x %08x\n", cmd, cmd2);
				//BPCmd bp = FifoAnalyzer::DecodeBPCmd(cmd2, m_BpMem); // TODO

				//FifoAnalyzer::LoadBPReg(bp, m_BpMem);
				// TODO: Load BP reg..

				// TODO
//				if (bp.address == BPMEM_TRIGGER_EFB_COPY)
//					StoreEfbCopyRegion();

				if ((regid == BPMEM_TRIGGER_EFB_COPY
					|| regid == BPMEM_CLEARBBOX1
					|| regid == BPMEM_CLEARBBOX2
					|| regid == BPMEM_SETDRAWDONE
					|| regid == BPMEM_PE_TOKEN_ID // TODO: Sure that we want to skip this one?
					|| regid == BPMEM_PE_TOKEN_INT_ID
					|| regid == BPMEM_LOADTLUT0
					|| regid == BPMEM_LOADTLUT1
					|| regid == BPMEM_TEXINVALIDATE
					|| regid == BPMEM_PRELOAD_MODE
					|| regid == BPMEM_CLEAR_PIXEL_PERF))
					nontrivial_command = true;
				
				break;
			}

			default:
				if (cmd & 0x80)
				{
					nontrivial_command = true;
					drawing_object = true;
					u32 vtxAttrGroup = cmd & GX_VAT_MASK;
					int vertex_size = CalculateVertexSize(vtxAttrGroup, cpmem);

					u16 stream_size = ReadFifo16(data);
					data += stream_size * vertex_size;
				}
				else
				{
					printf("Invalid fifo command 0x%x\n", cmd);
//					sleep(1);
				}
				break;
		}
		return data - data_start;
	}

private:
	bool m_drawingObject;

	CPMemory m_cpmem;
};

#endif  // FIFOPLAYER_FIFOANALYZER_H
