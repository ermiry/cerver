#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
#include "cerver/files/images.h"
#include "cerver/files/internal.h"
#include "cerver/handler/errors.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region images

// the bytes that are read to find the type of an image
#define FILES_IMAGE_HEADER_SIZE			24

// a BMP starts with "BM", and its DIB header (after the 14 bytes of the file header) starts with its own size
#define FILES_IMAGE_BMP_DIB_OFFSET		14
#define FILES_IMAGE_BMP_DIB_SIZES		7
#define FILES_IMAGE_BYTE_BITS			8
#define FILES_IMAGE_UINT32_BYTES		4

// the sizes of BITMAPCOREHEADER, BITMAPINFOHEADER, the two of Adobe, OS22XBITMAPHEADER, V4 and V5
static const unsigned int files_image_bmp_dib_sizes[FILES_IMAGE_BMP_DIB_SIZES] = {
	12, 40, 52, 56, 64, 108, 124
};

static const char *const image_bmp_extension = { "bmp" };
static const char *const image_gif_extension = { "gif" };
static const char *const image_png_extension = { "png" };
static const char *const image_jpg_extension = { "jpg" };
static const char *const image_jpeg_extension = { "jpeg" };

const char *files_image_type_to_string (const ImageType type) {

	switch (type) {
		#define XX(num, name, string, extension) case IMAGE_TYPE_##name: return #string;
		IMAGE_TYPE_MAP(XX)
		#undef XX
	}

	return files_image_type_to_string (IMAGE_TYPE_NONE);

}

const char *files_image_type_extension (const ImageType type) {

	switch (type) {
		#define XX(num, name, string, extension) case IMAGE_TYPE_##name: return #extension;
		IMAGE_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

// a little endian u32 of the header
static unsigned int files_image_read_uint32 (const unsigned char *bytes) {

	unsigned int value = 0;
	for (unsigned int i = FILES_IMAGE_UINT32_BYTES; i > 0; i--) {
		value = (value << FILES_IMAGE_BYTE_BITS) | bytes[i - 1];
	}

	return value;

}

// "BM" alone is too common for a text file, the size of its DIB header has to be a known one
static bool files_image_is_bmp (const unsigned char *header) {

	bool bmp = false;

	if ((header[0] == 'B') && (header[1] == 'M')) {
		const unsigned int dib_size = files_image_read_uint32 (header + FILES_IMAGE_BMP_DIB_OFFSET);
		for (unsigned int i = 0; (i < FILES_IMAGE_BMP_DIB_SIZES) && !bmp; i++) {
			bmp = (dib_size == files_image_bmp_dib_sizes[i]);
		}
	}

	return bmp;

}

ImageType files_image_get_type_from_file (const void *file_ptr) {

	FILE *file = (FILE *) file_ptr;

	ImageType type = IMAGE_TYPE_NONE;

	(void) fseek (file, 0, SEEK_END);
	long len = ftell (file);
	(void) fseek (file, 0, SEEK_SET);
	if (len > FILES_IMAGE_HEADER_SIZE) {
		unsigned char buf[FILES_IMAGE_HEADER_SIZE] = { 0 };

		// a short read leaves the header zeroed, it has no known type then
		if (fread (buf, 1, FILES_IMAGE_HEADER_SIZE, file) < FILES_IMAGE_HEADER_SIZE) {
			(void) memset (buf, 0, FILES_IMAGE_HEADER_SIZE);
		}

		if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
			type = IMAGE_TYPE_JPEG;
		}

		else if (
			buf[0] == 0x89 && buf[1] == 'P' && buf[2] =='N' && buf[3] =='G'
			&& buf[4] == 0x0D && buf[5] == 0x0A && buf[6] ==0x1A && buf[7] ==0x0A
			&& buf[12] == 'I' && buf[13] == 'H' && buf[14] =='D' && buf[15] =='R'
		) {
			type = IMAGE_TYPE_PNG;
		}

		else if (buf[0] =='G' && buf[1] =='I' && buf[2] =='F') {
			type = IMAGE_TYPE_GIF;
		}

		else if (files_image_is_bmp (buf)) {
			type = IMAGE_TYPE_BMP;
		}
	}

	return type;

}

ImageType files_image_get_type (const char *filename) {

	ImageType type = IMAGE_TYPE_NONE;

	if (!filename) return type;

	FILE *file = fopen (filename, "rb");
	if (file) {
		type = files_image_get_type_from_file (file);

		(void) fclose (file);
	}

	return type;

}

ImageType files_image_get_type_with_extension (const char *filename) {

	ImageType type = IMAGE_TYPE_NONE;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (!strcmp (image_png_extension, ext)) {
			type = IMAGE_TYPE_PNG;
		}

		else if (
			!strcmp (image_jpg_extension, ext)
			|| !strcmp (image_jpeg_extension, ext)
		) {
			type = IMAGE_TYPE_JPEG;
		}

		else if (!strcmp (image_bmp_extension, ext)) {
			type = IMAGE_TYPE_BMP;
		}

		else if (!strcmp (image_gif_extension, ext)) {
			type = IMAGE_TYPE_GIF;
		}
	}

	return type;

}

bool files_image_is_jpeg (const char *filename) {

	bool result = false;

	switch (files_image_get_type (filename)) {
		case IMAGE_TYPE_JPEG:
			result = true;
			break;

		default: break;
	}

	return result;

}

bool files_image_extension_is_jpeg (const char *filename) {

	bool retval = false;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (
			!strcmp (image_jpg_extension, ext)
			|| !strcmp (image_jpeg_extension, ext)
		) {
			retval = true;
		}
	}

	return retval;

}

bool files_image_is_png (const char *filename) {

	return (files_image_get_type (filename) == IMAGE_TYPE_PNG);

}

bool files_image_extension_is_png (const char *filename) {

	bool retval = false;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (!strcmp (image_png_extension, ext)) {
			retval = true;
		}
	}

	return retval;

}

#pragma endregion
