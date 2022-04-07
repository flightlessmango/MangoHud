#pragma once
enum EngineTypes
{
   UNKNOWN,

   OPENGL,
   VULKAN,

   DXVK,
   VKD3D,
   DAMAVAND,
   ZINK,

   WINED3D,
   FERAL3D,
   TOGL,

   GAMESCOPE
};

extern const char* engines[];
