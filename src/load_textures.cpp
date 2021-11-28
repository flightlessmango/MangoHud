#include "load_textures.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"

bool GL_LoadTextureFromFile(const char* filename, GLuint* out_texture, int* out_width, int* out_height, int maxwidth)
{
    // load the image
    int image_width = 0;
    int image_height = 0;
    int channels;
    auto image_data = stbi_load(filename, &image_width, &image_height, &channels, STBI_rgb_alpha);
    if (!image_data)
        return false;

    // reduce the image
    float ratio = 1;
    if (image_width > maxwidth && maxwidth != 0) {
        ratio = maxwidth / static_cast<float>(image_width);
    }

    int image_width_resized  = image_width * ratio;
    int image_height_resized = image_height * ratio;

    if (ratio != 1)
    {
        unsigned char* image_data_resized = (unsigned char*)stbi__malloc(image_width_resized * image_height_resized * 4);
        if (!image_data_resized) {
            stbi_image_free(image_data);
            return false;
        }

        stbir_resize_uint8(image_data, image_width, image_height, 0,
                            image_data_resized, image_width_resized, image_height_resized, 0,
                            4);
        stbi_image_free(image_data);
        image_data = image_data_resized;
    }

    // Create a OpenGL texture identifier
    GLuint image_texture;
    glGenTextures(1, &image_texture);
    glBindTexture(GL_TEXTURE_2D, image_texture);

    // Setup filtering parameters for display
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // This is required on WebGL for non power-of-two textures
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE); // Same

    // Upload pixels into texture
    #if defined(GL_UNPACK_ROW_LENGTH) && !defined(__EMSCRIPTEN__)
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    #endif
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image_width_resized, image_height_resized, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);

    *out_texture = image_texture;
    *out_width   = image_width_resized;
    *out_height  = image_height_resized;

    return true;
}
