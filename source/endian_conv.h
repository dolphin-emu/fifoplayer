#ifndef ENDIAN_CONV_H
#define ENDIAN_CONV_H

#ifdef HAVE_ENDIAN_H
#include <endian.h>
#else // asumming libogc
#include <gctypes.h>
#endif

#if BYTE_ORDER==BIG_ENDIAN
static inline uint64_t le64toh(uint64_t val)
{
	return ((val&0xff)<<56)|((val&0xff00)<<40)|((val&0xff0000)<<24)|((val&0xff000000)<<8) |
		((val&0xff00000000)>>8)|((val&0xff0000000000)>>24)|((val&0xff000000000000)>>40)|((val&0xff00000000000000)>>56);
}

static inline uint32_t le32toh(uint32_t val)
{
	return ((val&0xff)<<24)|((val&0xff00)<<8)|((val&0xff0000)>>8)|((val&0xff000000)>>24);
}

static inline uint32_t h32tole(uint32_t val)
{
	return le32toh(val);
}

static inline uint16_t le16toh(uint16_t val)
{
	return ((val&0xff)<<8)|((val&0xff00)>>8);
}

static inline uint32_t h16tole(uint32_t val)
{
	return le16toh(val);
}

static inline uint64_t be64toh(uint64_t val)
{
	return val;
}

static inline uint32_t be32toh(uint32_t val)
{
	return val;
}

static inline uint16_t be16toh(uint16_t val)
{
	return val;
}

static inline uint32_t htobe32(uint32_t val)
{
	return val;
}


#elif BYTE_ORDER==LITTLE_ENDIAN
// endian.h should have taken care of defining these...
#endif

#endif
