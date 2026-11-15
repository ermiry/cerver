#ifndef _CERVER_FILES_IMAGES_H_
#define _CERVER_FILES_IMAGES_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region images

#define IMAGE_TYPE_MAP(XX)						\
	XX(0,	NONE, 		None, 		undefined)	\
	XX(1,	PNG, 		PNG,		png)		\
	XX(2,	JPEG, 		JPEG, 		jpeg)		\
	XX(3,	GIF, 		GIF,		gif)		\
	XX(4,	BMP, 		BMP,		bmp)

typedef enum ImageType {

	#define XX(num, name, string, extension) IMAGE_TYPE_##name = num,
	IMAGE_TYPE_MAP (XX)
	#undef XX

} ImageType;

CERVER_EXPORT const char *files_image_type_to_string (
	const ImageType type
);

CERVER_EXPORT const char *files_image_type_extension (
	const ImageType type
);

// reads the file's contents to find matching magic bytes
CERVER_EXPORT ImageType files_image_get_type_from_file (
	const void *file
);

// opens the file and returns the file's image type
CERVER_EXPORT ImageType files_image_get_type (
	const char *filename
);

// returns the correct image type based on the filename's extension
CERVER_EXPORT ImageType files_image_get_type_with_extension (
	const char *filename
);

// returns true if jpeg magic bytes are in file
CERVER_EXPORT bool files_image_is_jpeg (const char *filename);

// returns true if the filename's extension is jpg or jpeg
CERVER_EXPORT bool files_image_extension_is_jpeg (
	const char *filename
);

// returns true if png magic bytes are in file
CERVER_EXPORT bool files_image_is_png (const char *filename);

// returns true if the filename's extension is png
CERVER_EXPORT bool files_image_extension_is_png (
	const char *filename
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
