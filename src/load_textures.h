#pragma once
#include <glad/glad.h>

bool GL_LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height, int maxwidth);
