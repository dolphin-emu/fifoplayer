#ifndef BITFIELD_H
#define BITFIELD_H

#include "CommonTypes.h"

// NOTE: Only works for sizeof(T)<=4 bytes
// NOTE: Only(?) works for unsigned fields?
template<u32 position, u32 bits, typename T=u32>
struct BitField
{
	BitField& operator = (u32 val)
	{
		storage = (storage & ~GetMask()) | ((val<<position) & GetMask());
		return *this;
	}

	operator u32() { return (storage & GetMask()) >> position; }

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

public: // TODO: Expose this properly
	T storage;
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
class BitFieldWrapper
{
public:
	template<u32 bf_position, u32 bf_bits>
	BitFieldWrapper(BitField<bf_position,bf_bits>& bitfield) : position(bf_position), bits(bf_bits), storage(*(u32*)&bitfield)
	{
	}

	BitFieldWrapper& operator = (u32 val)
	{
		storage = (storage & ~GetMask()) | ((val<<position) & GetMask());
		return *this;
	}

	operator u32() { return (storage & GetMask()) >> position; }

	u32 GetMask() const
	{
		return ((bits == 32) ? 0xFFFFFFFF : ((1 << bits)-1)) << position;
	}

	u32 MaxVal() const
	{
		// TODO
		return (1<<bits)-1;
	}

	u32 NumBits() const { return bits; }

private:
	u32 position;
	u32 bits;
public: // TODO:Expose this properly
	u32& storage;
};

#endif
