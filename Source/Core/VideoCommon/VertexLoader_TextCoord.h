// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.
// Modified for Ishiiruka by Tino
#pragma once
#include "VideoCommon/NativeVertexFormat.h"

class VertexLoader_TextCoord
{
public:

  // Init
  static void Init(void);

  // GetSize
  static u32 GetSize(u32 _type, u32 _format, u32 _elements);

  // GetFunction
  static TPipelineFunction GetFunction(u32 _type, u32 _format, u32 _elements);

  // GetDummyFunction
  // It is important to synchronize tcIndex.
  static TPipelineFunction GetDummyFunction();
private:
  static bool Initialized;
};