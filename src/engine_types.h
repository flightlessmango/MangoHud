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

   GAMESCOPE,
   LSFG
};

extern const char* engines[];
extern const char* engines_short[];
