// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#ifndef VERTEXLOADER_TEXCOORD_H
#define VERTEXLOADER_TEXCOORD_H

//#include "NativeVertexFormat.h"

class VertexLoader_TextCoord
{
public:

	// Init
	static void Init(void);

	// GetSize
	static unsigned int GetSize(unsigned int _type, unsigned int _format, unsigned int _elements)
	{
		const int tableReadTexCoordVertexSize[4][8][2] =
		{
			{
				{0, 0,}, {0, 0,}, {0, 0,}, {0, 0,}, {0, 0,},
			},
			{
				{1, 2,}, {1, 2,}, {2, 4,}, {2, 4,}, {4, 8,},
			},
			{
				{1, 1,}, {1, 1,}, {1, 1,}, {1, 1,}, {1, 1,},
			},
			{
				{2, 2,}, {2, 2,}, {2, 2,}, {2, 2,}, {2, 2,},
			},
		};
		return tableReadTexCoordVertexSize[_type][_format][_elements];
	}

	// GetFunction
//	static TPipelineFunction GetFunction(unsigned int _type, unsigned int _format, unsigned int _elements);

	// GetDummyFunction
	// It is important to synchronize tcIndex.
//	static TPipelineFunction GetDummyFunction();
};

#endif
