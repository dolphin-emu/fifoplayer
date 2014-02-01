#ifndef BITFIELD_H
#define BITFIELD_H

#include "CommonTypes.h"
#include "endian_conv.h"
#include <assert.h>

// NOTE: Only works for sizeof(T)<=4 bytes
// NOTE: Only(?) works for unsigned fields?
template<u32 position, u32 bits, typename T=u32>
struct BitField
{
private:
	BitField(u32 val)
	{
		// This just doesn't make any sense at all
		assert(0);
	}

	BitField(const BitField& other)
	{
		// creates new storage => NOT what we want
		assert(0);
	}

	BitField& operator = (const BitField& other)
	{
		// creates new storage => NOT what we want
		assert(0);
	}

public:
	BitField& operator = (u32 val)
	{
		storage = (storage & ~GetMask()) | ((val<<position) & GetMask());
		return *this;
	}

	operator u32() const { return (storage & GetMask()) >> position; }

	static T MaxVal()
	{
		// TODO
		return (1<<bits)-1;
	}

private:
	constexpr u32 GetMask()
	{
		return ((bits == 32) ? 0xFFFFFFFF : ((1 << bits)-1)) << position;
	}

	T storage;

	friend class BitFieldWrapper;
};

/* Example:
 */
union SomeClass
{
	u32 hex;
	BitField<0,7> first_seven_bits;
	BitField<7,8> next_eight_bits;
	// ...
};


// Slow, non-templated bit-fields - required for Qt, since Q_OBJECT classes cannot be templated
// TODO: Too specialized now, needs to be moved to Qt!
// non-templated bitfields acting on big endian storages but using host values for assignment and (u32) casts
class BitFieldWrapper
{
private:
	BitFieldWrapper(u32 num) : storage(*(u32*)nullptr)
	{
		// ILLEGAL!!
		// This either means:
		// - you want to have a bitfield on a certain number. Use a union of this number with BitField instead
		// - you want to assign this bitfield to a number. use the assignment operator instead
		assert(0);
	}

	BitFieldWrapper& operator = (const BitFieldWrapper& bf)
	{
		// ILLEGAL!!
		// This either means:
		// - you want to have a bitfield object which references the same bitfield in a u32 as the parameter bf. use the copy constructor instead
		// - you want to assign this bitfield to the bitfield value of bf. cast bf to u32 and use the "operator =(u32)" instead.
		assert(0);
	}

public:
	BitFieldWrapper(const BitFieldWrapper& bf) : position(bf.position), bits(bf.bits), storage(bf.storage)
	{
		assert((u32)*this == (u32)bf);
		assert(storage == bf.storage);
	}

	template<u32 bf_position, u32 bf_bits>
	BitFieldWrapper(BitField<bf_position,bf_bits>& bitfield) : position(bf_position), bits(bf_bits), storage(*(u32*)&bitfield)
	{
		assert(storage == bitfield.storage);
	}

	BitFieldWrapper& operator = (u32 val)
	{
		u32 host_storage = be32toh(storage);
		host_storage = (host_storage & ~GetMask()) | ((val<<position) & GetMask());
		storage = htobe32(host_storage);
	}

	operator u32() const
	{
		u32 host_storage = be32toh(storage);
		return (host_storage & GetMask()) >> position;
	}

	u32 GetMask() const
	{
		return ((bits == 32) ? 0xFFFFFFFF : ((1 << bits)-1)) << position;
	}

	u32 MaxVal() const
	{
		return (bits == 32)?0xFFFFFFFF : ((1<<bits)-1);
	}

	u32 NumBits() const { return bits; }

	u32 RawValue() const { return storage; }

private:
	u32 position;
	u32 bits;
	u32& storage;
};

#endif
