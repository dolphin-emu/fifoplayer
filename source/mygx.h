#define _SHIFTL(v, s, w)	\
    ((u32) (((u32)(v) & ((0x01 << (w)) - 1)) << (s)))

#define MY_LOAD_CP_REG(x, y)			\
	do {								\
		wgPipe->U8 = 0x08;				\
		wgPipe->U8 = (u8)(x);			\
		wgPipe->U32 = (u32)(y);		\
	} while(0)

#define MY_LOAD_XF_REG(x, y)			\
	do {								\
		wgPipe->U8 = 0x10;				\
		wgPipe->U32 = (u32)((x)&0xffff);		\
		wgPipe->U32 = (u32)(y);		\
	} while(0)

#define MY_LOAD_XF_REGS(x, n)			\
	do {								\
		wgPipe->U8 = 0x10;				\
		wgPipe->U32 = (u32)(((((n)&0xffff)-1)<<16)|((x)&0xffff));				\
	} while(0)

static inline void MyWriteMtxPS4x2(register Mtx mt,register void *wgpipe)
{
	register f32 tmp0,tmp1,tmp2,tmp3;
	__asm__ __volatile__
		("psq_l %0,0(%4),0,0\n\
		  psq_l %1,8(%4),0,0\n\
		  psq_l %2,16(%4),0,0\n\
		  psq_l %3,24(%4),0,0\n\
		  psq_st %0,0(%5),0,0\n\
		  psq_st %1,0(%5),0,0\n\
		  psq_st %2,0(%5),0,0\n\
		  psq_st %3,0(%5),0,0"
		  : "=&f"(tmp0),"=&f"(tmp1),"=&f"(tmp2),"=&f"(tmp3)
		  : "b"(mt), "b"(wgpipe)
		  : "memory"
	);
}

static inline void MyWriteMtxPS4x3(register Mtx mt,register void *wgpipe)
{
	register f32 tmp0,tmp1,tmp2,tmp3,tmp4,tmp5;
	__asm__ __volatile__ (
		 "psq_l %0,0(%6),0,0\n\
		  psq_l %1,8(%6),0,0\n\
		  psq_l %2,16(%6),0,0\n\
		  psq_l %3,24(%6),0,0\n\
		  psq_l %4,32(%6),0,0\n\
		  psq_l %5,40(%6),0,0\n\
		  psq_st %0,0(%7),0,0\n\
		  psq_st %1,0(%7),0,0\n\
		  psq_st %2,0(%7),0,0\n\
		  psq_st %3,0(%7),0,0\n\
		  psq_st %4,0(%7),0,0\n\
		  psq_st %5,0(%7),0,0"
		  : "=&f"(tmp0),"=&f"(tmp1),"=&f"(tmp2),"=&f"(tmp3),"=&f"(tmp4),"=&f"(tmp5)
		  : "b"(mt), "b"(wgpipe)
		  : "memory"
	);
}

void MY_LoadPosMtxImm(Mtx mt,u32 pnidx)
{
	MY_LOAD_XF_REGS((0x0000|(_SHIFTL(pnidx,2,8))),12);
	MyWriteMtxPS4x3(mt,(void*)wgPipe);
}

void MY_SetViewport(f32 xOrig,f32 yOrig,f32 wd,f32 ht,f32 nearZ,f32 farZ)
{
	f32 x0,y0,x1,y1,n,f,z;
	static f32 Xfactor = 0.5;
	static f32 Yfactor = 342.0;
	static f32 Zfactor = 16777215.0;

	x0 = wd*Xfactor;
	y0 = (-ht)*Xfactor;
	x1 = (xOrig+(wd*Xfactor))+Yfactor;
	y1 = (yOrig+(ht*Xfactor))+Yfactor;
	n = Zfactor*nearZ;
	f = Zfactor*farZ;
	z = f-n;

	MY_LOAD_XF_REGS(0x101a,6);
	wgPipe->F32 = x0;
	wgPipe->F32 = y0;
	wgPipe->F32 = z;
	wgPipe->F32 = x1;
	wgPipe->F32 = y1;
	wgPipe->F32 = f;
}

