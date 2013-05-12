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

#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>

#define DEFAULT_FIFO_SIZE   (256*1024)

static void *frameBuffer[2] = { NULL, NULL};
GXRModeObj *rmode;

u32 fb = 0;
u32 first_frame = 1;
Mtx view;
Mtx model, modelview;
void Init()
{
	f32 yscale;

	u32 xfbHeight;

	Mtx44 perspective;

	GXColor background = { 0, 0, 0, 0xff };

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

	// clears the bg to color and clears the z buffer
	GX_SetCopyClear(background, 0x00ffffff);

	// other gx setup
	GX_SetViewport(0,0,rmode->fbWidth,rmode->efbHeight,0,1);
	yscale = GX_GetYScaleFactor(rmode->efbHeight,rmode->xfbHeight);
	xfbHeight = GX_SetDispCopyYScale(yscale);
	GX_SetScissor(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopySrc(0,0,rmode->fbWidth,rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth,xfbHeight);
	GX_SetCopyFilter(rmode->aa,rmode->sample_pattern,GX_TRUE,rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering,((rmode->viHeight==2*rmode->xfbHeight)?GX_ENABLE:GX_DISABLE));

	if (rmode->aa)
		GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
	else
		GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

	GX_SetCullMode(GX_CULL_NONE);
	GX_CopyDisp(frameBuffer[fb],GX_TRUE);
	GX_SetDispCopyGamma(GX_GM_1_0);

	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);

	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);

	GX_SetNumChans(1);
	GX_SetNumTexGens(0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

	guVector cam = {0.0F, 0.0F, 0.0F},
	up = {0.0F, 1.0F, 0.0F},
	look = {0.0F, 0.0F, -1.0F};

	guLookAt(view, &cam, &up, &look);

	f32 w = rmode->viWidth;
	f32 h = rmode->viHeight;
	guPerspective(perspective, 45, (f32)w/h, 0.1F, 300.0F);
	GX_LoadProjectionMtx(perspective, GX_PERSPECTIVE);

	WPAD_Init();
}

#include "mygx.h"

int main()
{
	Init();

	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3); // Apply dirty state
	wgPipe->F32 = 0.0f; wgPipe->F32 = 1.0f; wgPipe->F32 = 0.0f; // Top
	wgPipe->F32 = -1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom left
	wgPipe->F32 = 1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom right
	GX_End();

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

		// Simple testing code
		// GX_SetViewport
		wgPipe->U8 = 0x10;
		wgPipe->U32 = (u32)((5<<16)|0x101a);
		wgPipe->F32 = rmode->fbWidth * 0.5;
		wgPipe->F32 = (-rmode->efbHeight) * 0.5;
		wgPipe->F32 = 1*16777215.0;
		wgPipe->F32 = rmode->fbWidth * 0.5 + 342.0;
		wgPipe->F32 = rmode->efbHeight * 0.5 + 342.0;
		wgPipe->F32 = 1*16777215.0;

		// InvVtxCache
		wgPipe->U8 = 0x48;

		guMtxIdentity(model);
		guMtxTransApply(model, model, -1.5f,0.0f,-6.0f);
		guMtxConcat(view,model,modelview);
		wgPipe->U8 = 0x10;
		wgPipe->U32 = (u32)((11<<16)|(_SHIFTL(GX_PNMTX0,2,8)));
		for (unsigned int i = 0;i < 12; ++i)
		{
			wgPipe->F32 = ((f32*)modelview)[i];
		}

		// Setup vtx desc
		wgPipe->U8 = 0x08;
		wgPipe->U8 = 0x50;
		wgPipe->U32 = _SHIFTL(GX_DIRECT,9,2);

		wgPipe->U8 = 0x08;
		wgPipe->U8 = 0x60;
		wgPipe->U32 = 0;

		wgPipe->U8 = 0x10;
		wgPipe->U32 = 0x1008;
		wgPipe->U32 = 0;

		// Draw a triangle
		wgPipe->U8 = GX_TRIANGLES|(GX_VTXFMT0&7);
		wgPipe->U16 = 3;
		wgPipe->F32 = 0.0f; wgPipe->F32 = 1.0f; wgPipe->F32 = 0.0f; // Top
		wgPipe->F32 = -1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom left
		wgPipe->F32 = 1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom right

		guMtxTransApply(model, model, 3.0f,0.0f,0.0f);
		guMtxConcat(view,model,modelview);
		wgPipe->U8 = 0x10;
		wgPipe->U32 = (u32)((11<<16)|(_SHIFTL(GX_PNMTX0,2,8)));
		for (unsigned int i = 0;i < 12; ++i)
		{
			wgPipe->F32 = ((f32*)modelview)[i];
		}

		// Draw a quad
		wgPipe->U8 = GX_QUADS|(GX_VTXFMT0&7);
		wgPipe->U16 = 4;
		wgPipe->F32 = -1.0f; wgPipe->F32 = 1.0f; wgPipe->F32 = 0.0f; // Top left
		wgPipe->F32 = 1.0f; wgPipe->F32 = 1.0f; wgPipe->F32 = 0.0f; // Top right
		wgPipe->F32 = 1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom right
		wgPipe->F32 = -1.0f; wgPipe->F32 = -1.0f; wgPipe->F32 = 0.0f; // Bottom left

		// finish frame...
		GX_DrawDone();
		GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
		GX_SetColorUpdate(GX_TRUE);
		GX_CopyDisp(frameBuffer[fb],GX_TRUE);

		VIDEO_SetNextFramebuffer(frameBuffer[fb]);
		if (first_frame)
		{
			VIDEO_SetBlack(FALSE);
			first_frame = 0;
		}

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

		if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME)
			processing = false;

		++cur_frame;
		cur_frame = first_frame + ((cur_frame-first_frame) % (last_frame-first_frame+1));
	}

	return 0;
}
