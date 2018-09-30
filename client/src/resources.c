#include "game.h"

/*** IMAGES ***/

// Include the STB image library - only the PNG support
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "utils/stb_image.h"

#include "resources.h"

#define SWAP_U32(x) (((x) >> 24) | (((x) & 0x00FF0000) >> 8) | (((x) & 0x0000FF00) << 8) | ((x) << 24))

BitmapImage *loadImageFromFile (char *filename) {

    int imgWidth, imgHeight, numComponents;
    unsigned char *imgData = stbi_load (filename, &imgWidth, &imgHeight, &numComponents, STBI_rgb_alpha);

    // copy the img data so we can hold onto it
    u32 imgDataSize = imgWidth * imgHeight * sizeof (u32);
    u32 *imageData = (u32 *) calloc (imgDataSize, sizeof (u32));
    memcpy (imageData, imgData, imgDataSize);

    /* if (system_is_little_endian ()) {
        u32 pixelCount = imgWidth * imgHeight;
        for (u32 i = 0; i < pixelCount; i++)
            imageData[i] = SWAP_U32 (imageData[i]);

    } */

    BitmapImage *bmi = (BitmapImage *) malloc (sizeof (BitmapImage));
    bmi->pixels = imageData;
    bmi->width = imgWidth;
    bmi->height = imgHeight;

    stbi_image_free (imgData);

    return bmi;

}

void destroyImage (BitmapImage *image) {

    if (image != NULL) {
        free (image->pixels);
        free (image);
    }

}