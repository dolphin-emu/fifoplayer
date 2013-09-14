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

#pragma pack(push, 4)
struct CPMemory
{
	TVtxDesc vtxDesc;
	VAT vtxAttr[8];
	u32 arrayBases[16];
	u32 arrayStrides[16];
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

static void LoadCPReg(u32 subCmd, u32 value, CPMemory &cpMem)
{
	switch (subCmd & 0xF0)
	{
	case 0x50:
		cpMem.vtxDesc.Hex &= ~0x1FFFF;  // keep the Upper bits
		cpMem.vtxDesc.Hex |= value;
		break;

	case 0x60:
		cpMem.vtxDesc.Hex &= 0x1FFFF;  // keep the lower 17Bits
		cpMem.vtxDesc.Hex |= (u64)value * 131072;
		break;

	case 0x70:
		cpMem.vtxAttr[subCmd & 7].g0.Hex = value;
		break;

	case 0x80:
		cpMem.vtxAttr[subCmd & 7].g1.Hex = value;
		break;

	case 0x90:
		cpMem.vtxAttr[subCmd & 7].g2.Hex = value;
		break;

	case 0xA0:
		cpMem.arrayBases[subCmd & 0xF] = value;
		break;

	case 0xB0:
		cpMem.arrayStrides[subCmd & 0xF] = value & 0xFF;
		break;
	}
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
	AnalyzedObject(u32 start, u32 end) : start(start), end(end) {}

	u32 start; // Address of first command in a polygon rendering series
	u32 end;  // Address of first command after rendering polygons

	std::vector<u32> cmd_starts;
	std::vector<bool> cmd_enabled;
};

struct AnalyzedFrameInfo
{
	std::vector<AnalyzedObject> objects;

	// TODO: These should be replaced in favor of the members in AnalyzedObject
	std::vector<u32> cmd_starts; // Address of each command of the frame
	std::vector<bool> cmd_enabled; // Whether to process this command or not

//	std::vector<MemoryUpdate> memory_updates;
};

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
			AnalyzedObject* cur_object = NULL;

			u32 cmd_start = 0;

			while (cmd_start < src_frame.fifoData.size())
			{
				bool was_drawing = m_drawingObject;
				u32 cmd_size = DecodeCommand(&src_frame.fifoData[cmd_start]);

				// TODO: Check that cmd_size != 0

				if (was_drawing != m_drawingObject)
				{
					if (m_drawingObject)
					{
						dst_frame.objects.push_back(AnalyzedObject(cmd_start, cmd_start));
						cur_object = &dst_frame.objects.back();
					}
					else
					{
						// TODO: Should make sure that the first command is always a draw command...
						dst_frame.objects.back().end = cmd_start;
					}
				}
				dst_frame.cmd_starts.push_back(cmd_start);
				dst_frame.cmd_enabled.push_back(true);
				if (cur_object)
				{
					cur_object->cmd_starts.push_back(cmd_start);
					cur_object->cmd_enabled.push_back(true);
				}
				cmd_start += cmd_size;
			}
			if (m_drawingObject)
				dst_frame.objects.back().end = cmd_start;
		}
	}

	u32 DecodeCommand(u8* data)
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
