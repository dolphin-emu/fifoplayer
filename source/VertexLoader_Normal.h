// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef _VERTEXLOADER_NORMAL_H
#define _VERTEXLOADER_NORMAL_H

//#include "Common.h"
//#include "CommonTypes.h"

class VertexLoader_Normal
{
public:

	// Init
	static void Init(void);

	// GetFunction
//	static TPipelineFunction GetFunction(unsigned int _type,
//		unsigned int _format, unsigned int _elements, unsigned int _index3);

private:
	enum ENormalType
	{
		NRM_NOT_PRESENT		= 0,
		NRM_DIRECT			= 1,
		NRM_INDEX8			= 2,
		NRM_INDEX16			= 3,
		NUM_NRM_TYPE
	};

	enum ENormalFormat
	{
		FORMAT_UBYTE		= 0,
		FORMAT_BYTE			= 1,
		FORMAT_USHORT		= 2,
		FORMAT_SHORT		= 3,
		FORMAT_FLOAT		= 4,
		NUM_NRM_FORMAT
	};

	static int FormatBaseSize(int format)
	{
		if (format == FORMAT_UBYTE) return 1;
		else if (format == FORMAT_BYTE) return 1;
		else if (format == FORMAT_USHORT) return 2;
		else if (format == FORMAT_SHORT) return 2;
		else if (format == FORMAT_FLOAT) return sizeof(float);
		else return 0;
	}

	enum ENormalElements
	{
		NRM_NBT				= 0,
		NRM_NBT3			= 1,
		NUM_NRM_ELEMENTS
	};

	enum ENormalIndices
	{
		NRM_INDICES1		= 0,
		NRM_INDICES3		= 1,
		NUM_NRM_INDICES
	};

/*	struct Set
	{
		template <typename T>
		void operator=(const T&)
		{
			gc_size = T::size;
			function = T::function;
		}
		
		int gc_size;
//		TPipelineFunction function;
	};*/

//	static Set m_Table[NUM_NRM_TYPE][NUM_NRM_INDICES][NUM_NRM_ELEMENTS][NUM_NRM_FORMAT];

public:
	// GetSize
	static unsigned int GetSize(unsigned int _type, unsigned int _format,
		unsigned int _elements, unsigned int _index3)
	{
		if (_type == NRM_DIRECT)
		{
			int base_size = FormatBaseSize(_format);
			int num = (_elements == NRM_NBT) ? 1 : 3;

			return base_size * num * 3;
		}
		else if (_type == NRM_INDEX8 || _type == NRM_INDEX16)
		{
			int base_size = (_type == NRM_INDEX8) ? 1 : 2;
			if (_index3 == NRM_INDICES1)
				return base_size;
			else
			{
				if ( _elements == NRM_NBT)
					return base_size;
				else
					return 3 * base_size;
			}
		}
		else
			return 0;

	}
};

#endif
