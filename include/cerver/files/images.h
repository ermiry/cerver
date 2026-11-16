/**
 * @file cerver/files/images.h
 * @brief The types of the image files, by their magic bytes or by their extension.
 */

#ifndef _CERVER_FILES_IMAGES_H_
#define _CERVER_FILES_IMAGES_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_images Images
 * @ingroup files
 * @brief The image types.
 * @{
 */

/**
 * @brief The X-macro table of the image types: the number, the name, the string and the extension of each one.
 */
#define IMAGE_TYPE_MAP(XX)						\
	XX(0,	NONE, 		None, 		undefined)	\
	XX(1,	PNG, 		PNG,		png)		\
	XX(2,	JPEG, 		JPEG, 		jpeg)		\
	XX(3,	GIF, 		GIF,		gif)		\
	XX(4,	BMP, 		BMP,		bmp)

/**
 * @brief The type of an image, one `IMAGE_TYPE_<name>` for each row of #IMAGE_TYPE_MAP.
 */
typedef enum ImageType {

	#define XX(num, name, string, extension) IMAGE_TYPE_##name = num,
	IMAGE_TYPE_MAP (XX)
	#undef XX

} ImageType;

/**
 * @brief Returns the string of an image type.
 *
 * @param[in] type the type
 * @return the string of its row in #IMAGE_TYPE_MAP, or the one of IMAGE_TYPE_NONE for a value out of the table; a
 * static string that must not be freed
 */
CERVER_EXPORT const char *files_image_type_to_string (
	const ImageType type
);

/**
 * @brief Returns the extension of an image type.
 *
 * @param[in] type the type
 * @return the extension of its row in #IMAGE_TYPE_MAP, NULL for a value out of the table; a static string that
 * must not be freed
 */
CERVER_EXPORT const char *files_image_type_extension (
	const ImageType type
);

/**
 * @brief Finds the type of an open file by the magic bytes of its first 24 bytes: JPEG, PNG, GIF or BMP.
 *
 * A BMP starts with "BM" and the size of a known DIB header (12, 40, 52, 56, 64, 108 or 124) at its byte 14.
 *
 * @param[in] file the FILE, it must not be NULL; it is left at the end of its header
 * @return the type, IMAGE_TYPE_NONE for a file of 24 bytes or less or of another type
 */
CERVER_EXPORT ImageType files_image_get_type_from_file (
	const void *file
);

/**
 * @brief Opens a file and finds its type by its magic bytes, see files_image_get_type_from_file ().
 *
 * @param[in] filename the path of the file
 * @return the type, IMAGE_TYPE_NONE for a NULL filename or a file that can not be opened
 */
CERVER_EXPORT ImageType files_image_get_type (
	const char *filename
);

/**
 * @brief Returns the type of an image by the extension of its filename: png, jpg or jpeg, bmp, gif.
 *
 * @param[in] filename the filename
 * @return the type, IMAGE_TYPE_NONE for another extension or none
 */
CERVER_EXPORT ImageType files_image_get_type_with_extension (
	const char *filename
);

/**
 * @brief Returns whether a file has the magic bytes of a JPEG.
 *
 * @param[in] filename the path of the file
 * @return true for a JPEG
 */
CERVER_EXPORT bool files_image_is_jpeg (const char *filename);

/**
 * @brief Returns whether the extension of a filename is jpg or jpeg.
 *
 * @param[in] filename the filename
 * @return true for a jpg or a jpeg extension
 */
CERVER_EXPORT bool files_image_extension_is_jpeg (
	const char *filename
);

/**
 * @brief Returns whether a file has the magic bytes of a PNG.
 *
 * @param[in] filename the path of the file
 * @return true for a PNG
 */
CERVER_EXPORT bool files_image_is_png (const char *filename);

/**
 * @brief Returns whether the extension of a filename is png.
 *
 * @param[in] filename the filename
 * @return true for a png extension
 */
CERVER_EXPORT bool files_image_extension_is_png (
	const char *filename
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
