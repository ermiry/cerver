/*  Base code written by Peter de Tagyos
    Started: 12/24/2015
    Github page: https://github.com/pdetagyos */

#include <assert.h>
#include <string.h>

#include "game.h"
#include "ui/console.h"
#include "utils/stb_image.h"

#define COLOR_FROM_RGBA(r, g, b, a) ((r << 24) | (g << 16) | (b << 8) | a)

void fill (u32 *pixels, u32 pixelsPerRow, Rect *destRect, u32 color) {

    u32 stopX = destRect->x + destRect->w;
    u32 stopY = destRect->y + destRect->h;

    for (u32 dstY = destRect->y; dstY < stopY; dstY++) {
        for (u32 dstX = destRect->x; dstX < stopX; dstX++) {
            pixels[(dstY * pixelsPerRow) + dstX] = color;
        }
    }

}

void clearConsole (Console *console) {

    Rect rect = { 0, 0, console->width, console->height };
    fill (console->pixels, console->width, &rect, console->bgColor);

}

Console *initConsole (i32 width, i32 height, i32 rowCount, i32 colCount, u32 bgColor, bool colorize) {

    Console *console = (Console *) malloc (sizeof (Console));

    console->pixels = (u32 *) calloc (width * height, sizeof (u32));
    console->width = width;
    console->height = height;
    console->rows = rowCount;
    console->cols = colCount;

    console->cellWidth = width / colCount;
    console->cellHeight = height / rowCount;

    console->bgColor = bgColor;
    console->colorize = colorize;

    console->font = NULL;
    console->cells = (Cell *) calloc (rowCount * colCount, sizeof (Cell));

    return console;

}

void destroyConsole (Console *console) {

    if (console->pixels) free (console->pixels);
    if (console->cells) free (console->cells);

    if (console) free (console);

}

void setConsoleBitmapFont (Console *console, char *filename, asciiChar firstCharInAtlas, int charWidth, int charHeight) {

    // load image data
    unsigned imgWidth, imgHeight, numComponents;
    unsigned char *imgData = stbi_load (filename, &imgWidth, &imgHeight, &numComponents, STBI_rgb_alpha);

    if (imgData == NULL) fprintf (stderr, "Error loading image!!\n\n");

    // Copy the image data so we can hold onto it
    u32 imgDataSize = imgWidth * imgHeight * sizeof(u32);
    u32 *atlasData = (u32 *) malloc (imgDataSize);
    memcpy (atlasData, imgData, imgDataSize);

    // Create and configure the font
    Font *font = (Font *) malloc (sizeof (Font));
    font->atlas = atlasData;
    font->charWidth = charWidth;
    font->charHeight = charHeight;
    font->atlasWidth = imgWidth;
    font->atlasHeight = imgHeight;
    font->firstCharInAtlas = firstCharInAtlas; 

    stbi_image_free (imgData);

    if (console->font != NULL) {
        free (console->font->atlas);
        free (console->font);
    }

    console->font = font;   

}

u32 colorizePixel (u32 dest, u32 src)  {

    // Colorize the destination pixel using the source color
    if (ALPHA(dest) == 255) return src;
    
    else if (ALPHA(dest) > 0) {
        // Scale the final alpha based on both dest & src alphas
        return COLOR_FROM_RGBA(RED(src), 
                               GREEN(src), 
                               BLUE(src), 
                               (u8)(ALPHA(src) * (ALPHA(dest) / 255.0)));
    } 
    
    else return dest;

}

void copyBlend (u32 *destPixels, Rect *destRect, u32 destPixelsPerRow,
             u32 *srcPixels, Rect *srcRect, u32 srcPixelsPerRow,
             u32 *newColor) {

    // If src and dest rects are not the same size ==> bad things
    assert(destRect->w == srcRect->w && destRect->h == srcRect->h);

    // For each pixel in the destination rect, alpha blend to it the 
    // corresponding pixel in the source rect.
    // ref: https://en.wikipedia.org/wiki/Alpha_compositing

    u32 stopX = destRect->x + destRect->w;
    u32 stopY = destRect->y + destRect->h;

    for (u32 dstY = destRect->y, srcY = srcRect->y; 
         dstY < stopY; 
         dstY++, srcY++) {
        for (u32 dstX = destRect->x, srcX = srcRect->x; 
             dstX < stopX; 
             dstX++, srcX++) {

            u32 srcColor = srcPixels[(srcY * srcPixelsPerRow) + srcX];
            u32 *destPixel = &destPixels[(dstY * destPixelsPerRow) + dstX];
            u32 destColor = *destPixel;

            // Colorize our source pixel before we blend it
            srcColor = colorizePixel(srcColor, *newColor);

            if (ALPHA(srcColor) == 0) {
                // Source is transparent - so do nothing
                continue;
            } else if (ALPHA(srcColor) == 255) {
                // Just copy the color, no blending necessary
                *destPixel = srcColor;
            } else {
                // Do alpha blending
                float srcA = ALPHA(srcColor) / 255.0;
                float invSrcA = (1.0 - srcA);
                float destA = ALPHA(destColor) / 255.0;

                float outAlpha = srcA + (destA * invSrcA);
                u8 fRed = ((RED(srcColor) * srcA) + (RED(destColor) * destA * invSrcA)) / outAlpha;
                u8 fGreen = ((GREEN(srcColor) * srcA) + (GREEN(destColor) * destA * invSrcA)) / outAlpha;
                u8 fBlue = ((BLUE(srcColor) * srcA) + (BLUE(destColor) * destA * invSrcA)) / outAlpha;
                u8 fAlpha = outAlpha * 255;

                *destPixel = COLOR_FROM_RGBA(fRed, fGreen, fBlue, fAlpha);
            }
        }
    }
}

void fillBlend(u32 *pixels, u32 pixelsPerRow, Rect *destRect, u32 color) {

    // For each pixel in the destination rect, alpha blend the 
    // bgColor to the existing color.
    // ref: https://en.wikipedia.org/wiki/Alpha_compositing

    u32 stopX = destRect->x + destRect->w;
    u32 stopY = destRect->y + destRect->h;

    // If the color we're trying to blend is transparent, then bail
    if (ALPHA(color) == 0) return;

    float srcA = ALPHA(color) / 255.0;
    float invSrcA = 1.0 - srcA;

    // Otherwise, blend each pixel in the dest rect
    for (u32 dstY = destRect->y; dstY < stopY; dstY++) {
        for (u32 dstX = destRect->x; dstX < stopX; dstX++) {
            u32 *pixel = &pixels[(dstY * pixelsPerRow) + dstX];

            if (ALPHA(color) == 255) {
                // Just copy the color, no blending necessary
                *pixel = color;
            } else {
                // Do alpha blending
                u32 pixelColor = *pixel;

                float destA = ALPHA(pixelColor) / 255.0;

                float outAlpha = srcA + (destA * invSrcA);
                u8 fRed = ((RED(color) * srcA) + (RED(pixelColor) * destA * invSrcA)) / outAlpha;
                u8 fGreen = ((GREEN(color) * srcA) + (GREEN(pixelColor) * destA * invSrcA)) / outAlpha;
                u8 fBlue = ((BLUE(color) * srcA) + (BLUE(pixelColor) * destA * invSrcA)) / outAlpha;
                u8 fAlpha = outAlpha * 255;

                *pixel = COLOR_FROM_RGBA(fRed, fGreen, fBlue, fAlpha);
            }
        }
    }
}

Rect rectForGlyph(asciiChar c, Font *font) {

    i32 idx = c - font->firstCharInAtlas;
    i32 charsPerRow = (font->atlasWidth / font->charWidth);
    i32 xOffset = (idx % charsPerRow) * font->charWidth;
    i32 yOffset = (idx / charsPerRow) * font->charHeight;

    Rect glyphRect = {xOffset, yOffset, font->charWidth, font->charHeight};

    return glyphRect;

}

void putCharAt (Console *console, asciiChar c, 
            i32 cellX, i32 cellY, u32 fgColor, u32 bgColor) {

    i32 x = cellX * console->cellWidth;
    i32 y = cellY * console->cellHeight;

    Rect destRect = { x, y, console->cellWidth, console->cellHeight };

    // Fill the background with alpha blending
    fillBlend (console->pixels, console->width, &destRect, bgColor);

    // Copy the glyph with alpha blending and desired coloring
    Rect srcRect = rectForGlyph (c, console->font);
    copyBlend (console->pixels, &destRect, console->width, 
                 console->font->atlas, &srcRect, console->font->atlasWidth,
                 &fgColor);

}

void putStringAt (Console *con, char *string, i32 x, i32 y, u32 fgColor, u32 bgColor) {

    i32 len = strlen (string);
    for (i32 i = 0; i < len; i++)
        putCharAt (con, (asciiChar) string[i], x + i, y, fgColor, bgColor);

}

void putReverseString (Console *con, char *string, i32 x, i32 y, u32 fgColor, u32 bgColor) {

    i32 len = strlen (string);
    for (i32 i = len - 1; i >= 0; i--)
        putCharAt (con, (asciiChar) string[i], x - i, y, fgColor, bgColor);

}

void putStringAtCenter (Console *con, char *string, i32 y, u32 fgColor, u32 bgColor) {

    u32 stringLen = strlen (string);
    u32 x = (con->cols / 2) - (stringLen / 2);

    for (u8 i = 0; i < stringLen; i++)
        putCharAt (con, (asciiChar) string[i], x + i, y, fgColor, bgColor);

}

void putStringAtRect (Console *con, char *string, Rect rect, bool wrap, u32 fgColor, u32 bgColor) {

    u32 len = strlen (string);
    i32 x = rect.x;
    i32 x2 = x + rect.w;
    i32 y = rect.y;
    i32 y2 = y + rect.h;

    for (u32 i = 0; i < len; i++) {
        bool shoudPut = true;
        if (x >= x2) {
            if (wrap) {
                x = rect.x;
                y += 1;
            }

            else shoudPut = false;
        }

        if (y >= y2) shoudPut = false;

        if (shoudPut) {
            putCharAt (con, (asciiChar) string[i], x, y, fgColor, bgColor);
            x++;
        }
    }

}

