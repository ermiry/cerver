#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/files.h>

#include "test.h"

static const char *sanitize_complete_one = { "hola/adios/1234567890-hola.png" };
static const char *sanitize_complete_two = { "1234567890-hola/adios/hola.png.res" };
static const char *sanitize_complete_three = { "12345!67`890-ho#la@/ad$^ios&/h*o)l)a.p+=ng.res" };

static const char *bmp_extension = { "bmp" };
static const char *gif_extension = { "gif" };
static const char *png_extension = { "png" };
static const char *jpg_extension = { "jpg" };
static const char *jpeg_extension = { "jpeg" };

static const char *jpg_file_small = { "test.jpg" };
static const char *jpg_file_complete = { "/home/ermiry/Pictures/test.jpg" };
static const char *jpg_file_bad = { "test" };
static const char *jpg_file_wrong = { "/home/ermiry/Pictures/test" };

static const char *jpeg_file_small = { "test.jpeg" };
static const char *jpeg_file_complete = { "/home/ermiry/Pictures/test.jpeg" };
static const char *jpeg_file_bad = { "test" };
static const char *jpeg_file_wrong = { "/home/ermiry/Pictures/test" };

static const char *png_file_small = { "test.png" };
static const char *png_file_complete = { "/home/ermiry/Pictures/test.png" };
static const char *png_file_bad = { "test" };
static const char *png_file_wrong = { "/home/ermiry/Pictures/test" };

static const char *test_string = { "This is a test file" };
static const char *less_test_string = { "This is a test" };

#pragma region main

static void test_files_sanitize_complete_filename (void) {

	char buffer[FILENAME_DEFAULT_SIZE] = { 0 };

	(void) strncpy (buffer, sanitize_complete_one, FILENAME_DEFAULT_SIZE - 1);
	files_sanitize_complete_filename (buffer);
	test_check_str_eq (buffer, sanitize_complete_one, NULL);
	
	(void) strncpy (buffer, sanitize_complete_two, FILENAME_DEFAULT_SIZE - 1);
	files_sanitize_complete_filename (buffer);
	test_check_str_eq (buffer, sanitize_complete_two, NULL);

	(void) strncpy (buffer, sanitize_complete_three, FILENAME_DEFAULT_SIZE - 1);
	files_sanitize_complete_filename (buffer);
	test_check_str_eq (buffer, sanitize_complete_two, NULL);

}

static void test_files_create_dir (void) {

	(void) system ("rm -r hola");

	test_check_unsigned_eq (files_create_dir ("hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola"));

	test_check_unsigned_eq (files_create_dir ("test", 0777), 1, NULL);
	test_check_true (file_exists ("test"));

}

static void test_files_create_recursive_dir (void) {

	(void) system ("rm -r hola");

	test_check_unsigned_eq (files_create_recursive_dir ("hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola"));

	test_check_unsigned_eq (files_create_recursive_dir ("hola/adios/hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola/adios/hola"));

	(void) system ("rm -r hola");

	test_check_unsigned_eq (files_create_recursive_dir ("test/client", 0777), 1, NULL);
	test_check_true (file_exists ("test/client"));

	(void) rmdir ("test/client/hola");

	test_check_unsigned_eq (files_create_recursive_dir ("test/client/hola", 0777), 0, NULL);
	test_check_true (file_exists ("test/client/hola"));

	test_check_unsigned_eq (files_create_recursive_dir ("test/client/client.c", 0777), 1, NULL);
	test_check_true (file_exists ("test/client/client.c"));

}

static void test_files_get_file_extension_reference_jpg (void) {

	unsigned int ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (jpg_file_small, &ext_len), jpg_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (jpg_extension), NULL);

	ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (jpg_file_complete, &ext_len), jpg_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (jpg_extension), NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (jpg_file_bad, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (jpg_file_wrong, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

}

static void test_files_get_file_extension_reference_jpeg (void) {

	unsigned int ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (jpeg_file_small, &ext_len), jpeg_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (jpeg_extension), NULL);

	ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (jpeg_file_complete, &ext_len), jpeg_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (jpeg_extension), NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (jpeg_file_bad, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (jpeg_file_wrong, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

}

static void test_files_get_file_extension_reference_png (void) {

	unsigned int ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (png_file_small, &ext_len), png_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (png_extension), NULL);

	ext_len = 0;
	test_check_str_eq (files_get_file_extension_reference (png_file_complete, &ext_len), png_extension, NULL);
	test_check_unsigned_eq (ext_len, strlen (png_extension), NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (png_file_bad, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

	ext_len = 0;
	test_check_null_ptr (files_get_file_extension_reference (png_file_wrong, &ext_len));
	test_check_unsigned_eq (ext_len, 0, NULL);

}

static void test_files_get_file_extension_reference (void) {
	
	test_files_get_file_extension_reference_jpg ();
	test_files_get_file_extension_reference_jpeg ();
	test_files_get_file_extension_reference_png ();

}

static void test_file_read_text (void) {

	size_t file_size = 0;
	char *file_contents = file_read ("./test/data/test.txt", &file_size);

	test_check_ptr (file_contents);
	test_check_unsigned_eq (file_size, strlen (test_string), NULL);
	test_check_str_eq (file_contents, test_string, NULL);

	free (file_contents);

}

static void test_file_read_json (void)  {

	size_t file_size = 0;
	char *file_contents = file_read ("./test/data/small.json", &file_size);

	test_check_ptr (file_contents);
	
	(void) printf ("\n/%s/\n\n", file_contents);

	free (file_contents);

}

static void test_file_read (void) {

	test_file_read_text ();

	test_file_read_json ();

}

static void test_file_n_read_less_text (void) {

	size_t n_read = 0;
	char *file_contents = file_n_read ("./test/data/test.txt", 14, &n_read);

	test_check_ptr (file_contents);
	test_check_unsigned_eq (n_read, strlen (less_test_string), NULL);
	test_check_str_eq (file_contents, less_test_string, NULL);

	free (file_contents);

}

static void test_file_n_read_more_text (void) {

	size_t n_read = 0;
	char *file_contents = file_n_read ("./test/data/test.txt", 128, &n_read);

	test_check_ptr (file_contents);
	test_check_unsigned_eq (n_read, strlen (test_string), NULL);
	test_check_str_eq (file_contents, test_string, NULL);

	free (file_contents);

}

static void test_file_n_read (void) {

	test_file_n_read_less_text ();

	test_file_n_read_more_text ();

}

#pragma endregion

#pragma region images

static const char *bmp_type = { "BMP" };
static const char *gif_type = { "GIF" };
static const char *png_type = { "PNG" };
static const char *jpeg_type = { "JPEG" };
static const char *other_type = { "None" };

static void test_files_image_type_to_string (void) {

	test_check_str_eq (files_image_type_to_string (IMAGE_TYPE_NONE), other_type, NULL);

	test_check_str_eq (files_image_type_to_string (IMAGE_TYPE_PNG), png_type, NULL);
	test_check_str_eq (files_image_type_to_string (IMAGE_TYPE_JPEG), jpeg_type, NULL);
	test_check_str_eq (files_image_type_to_string (IMAGE_TYPE_GIF), gif_type, NULL);
	test_check_str_eq (files_image_type_to_string (IMAGE_TYPE_BMP), bmp_type, NULL);

	test_check_str_eq (files_image_type_to_string ((ImageType) 64), other_type, NULL);
	test_check_str_eq (files_image_type_to_string ((ImageType) 100), other_type, NULL);

}

static void test_files_image_get_type_by_extension (void) {
	
	test_check_unsigned_eq (files_image_get_type_by_extension (jpg_file_small), IMAGE_TYPE_JPEG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpg_file_complete), IMAGE_TYPE_JPEG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpg_file_bad), IMAGE_TYPE_NONE, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpg_file_wrong), IMAGE_TYPE_NONE, NULL);

	test_check_unsigned_eq (files_image_get_type_by_extension (jpeg_file_small), IMAGE_TYPE_JPEG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpeg_file_complete), IMAGE_TYPE_JPEG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpeg_file_bad), IMAGE_TYPE_NONE, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (jpeg_file_wrong), IMAGE_TYPE_NONE, NULL);

	test_check_unsigned_eq (files_image_get_type_by_extension (png_file_small), IMAGE_TYPE_PNG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (png_file_complete), IMAGE_TYPE_PNG, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (png_file_bad), IMAGE_TYPE_NONE, NULL);
	test_check_unsigned_eq (files_image_get_type_by_extension (png_file_wrong), IMAGE_TYPE_NONE, NULL);

}

static void test_files_image_extension_is_jpeg (void) {
	
	test_check_bool_eq (files_image_extension_is_jpeg (jpg_file_small), true, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpg_file_complete), true, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpg_file_bad), false, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpg_file_wrong), false, NULL);

	test_check_bool_eq (files_image_extension_is_jpeg (jpeg_file_small), true, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpeg_file_complete), true, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpeg_file_bad), false, NULL);
	test_check_bool_eq (files_image_extension_is_jpeg (jpeg_file_wrong), false, NULL);

}

static void test_files_image_extension_is_png (void) {
	
	test_check_bool_eq (files_image_extension_is_png (png_file_small), true, NULL);
	test_check_bool_eq (files_image_extension_is_png (png_file_complete), true, NULL);
	test_check_bool_eq (files_image_extension_is_png (png_file_bad), false, NULL);
	test_check_bool_eq (files_image_extension_is_png (png_file_wrong), false, NULL);

}

#pragma endregion

int main (int argc, char **argv) {

	(void) printf ("Testing FILES...\n");

	// main
	test_files_sanitize_complete_filename ();
	test_files_create_dir ();
	test_files_create_recursive_dir ();
	test_files_get_file_extension_reference ();
	test_file_read ();
	test_file_n_read ();

	// images
	test_files_image_type_to_string ();
	test_files_image_get_type_by_extension ();
	test_files_image_extension_is_jpeg ();
	test_files_image_extension_is_png ();

	(void) printf ("\nDone with FILES tests!\n\n");

	return 0;

}
