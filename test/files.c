#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>
#include <unistd.h>

#include <sys/stat.h>

#include <cerver/files.h>
#include <cerver/packets.h>

#include "test.h"

static const char *sanitize_complete_one = { "hola/adios/1234567890-hola.png" };
static const char *sanitize_complete_two = { "1234567890-hola/adios/hola.png.res" };
static const char *sanitize_complete_three = { "12345!67`890-ho#la@/ad$^ios&/h*o)l)a.p+=ng.res" };

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

	(void) files_remove_recursive ("hola");

	test_check_unsigned_eq (files_create_dir ("hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola"));

	test_check_unsigned_eq (files_create_dir ("test", 0777), 1, NULL);
	test_check_true (file_exists ("test"));

}

#define FILES_RECURSIVE_DIR_THREADS			8
#define FILES_RECURSIVE_DIR_ROUNDS			64
#define FILES_RECURSIVE_DIR_RACE_PATH		"hola/race/one/two/three"
#define FILES_RECURSIVE_DIR_LONG_SIZE		(DIRNAME_DEFAULT_SIZE * 2)
#define FILES_RECURSIVE_DIR_COMPONENT		200
#define FILES_RECURSIVE_DIR_LONG_ROOT		"/tmp/cerver-recursive"

static pthread_barrier_t files_recursive_dir_barrier;

static void *files_recursive_dir_race (void *result) {

	(void) pthread_barrier_wait (&files_recursive_dir_barrier);

	*(unsigned int *) result = files_create_recursive_dir (
		FILES_RECURSIVE_DIR_RACE_PATH, 0777
	);

	return NULL;

}

// the threads create the same path at the same time,
// a directory that another thread created first is not an error
static void test_files_create_recursive_dir_race (void) {

	pthread_t threads[FILES_RECURSIVE_DIR_THREADS] = { 0 };
	unsigned int results[FILES_RECURSIVE_DIR_THREADS] = { 0 };

	for (unsigned int round = 0; round < FILES_RECURSIVE_DIR_ROUNDS; round++) {
		(void) files_remove_recursive ("hola");

		test_check_int_eq (
			pthread_barrier_init (&files_recursive_dir_barrier, NULL, FILES_RECURSIVE_DIR_THREADS),
			0, NULL
		);

		for (unsigned int i = 0; i < FILES_RECURSIVE_DIR_THREADS; i++) {
			results[i] = 1;
			test_check_int_eq (
				pthread_create (&threads[i], NULL, files_recursive_dir_race, &results[i]), 0, NULL
			);
		}

		for (unsigned int i = 0; i < FILES_RECURSIVE_DIR_THREADS; i++) {
			(void) pthread_join (threads[i], NULL);
			test_check_unsigned_eq (results[i], 0, NULL);
		}

		(void) pthread_barrier_destroy (&files_recursive_dir_barrier);

		test_check_true (file_exists (FILES_RECURSIVE_DIR_RACE_PATH));
	}

	test_check_unsigned_eq (files_remove_recursive ("hola"), 0, NULL);

}

// a path that does not fit is an error and nothing is created,
// neither its truncated prefix
static void test_files_create_recursive_dir_long (void) {

	char path[FILES_RECURSIVE_DIR_LONG_SIZE] = { 0 };

	// in /tmp, so the absolute path is not longer than the one of the file system of the tree
	(void) files_remove_recursive (FILES_RECURSIVE_DIR_LONG_ROOT);

	// components shorter than NAME_MAX, the path ends with a slash
	for (size_t i = 0; i < (sizeof (path) - 1); i++) {
		path[i] = ((i % FILES_RECURSIVE_DIR_COMPONENT) == (FILES_RECURSIVE_DIR_COMPONENT - 1)) ? '/' : 'a';
	}

	(void) memcpy (path, FILES_RECURSIVE_DIR_LONG_ROOT "/", strlen (FILES_RECURSIVE_DIR_LONG_ROOT "/"));
	path[sizeof (path) - 2] = '/';

	test_check_unsigned_eq (files_create_recursive_dir (path, 0777), 1, NULL);
	test_check_false (file_exists (FILES_RECURSIVE_DIR_LONG_ROOT));

	// a path of DIRNAME_DEFAULT_SIZE does not fit with its NUL
	path[DIRNAME_DEFAULT_SIZE] = '\0';
	test_check_unsigned_eq (files_create_recursive_dir (path, 0777), 1, NULL);
	test_check_false (file_exists (FILES_RECURSIVE_DIR_LONG_ROOT));

	path[DIRNAME_DEFAULT_SIZE - 1] = '\0';
	test_check_unsigned_eq (files_create_recursive_dir (path, 0777), 0, NULL);
	test_check_true (file_exists (path));

	test_check_unsigned_eq (files_remove_recursive (FILES_RECURSIVE_DIR_LONG_ROOT), 0, NULL);

}

static void test_files_create_recursive_dir (void) {

	(void) files_remove_recursive ("hola");

	test_check_unsigned_eq (files_create_recursive_dir ("hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola"));

	test_check_unsigned_eq (files_create_recursive_dir ("hola/adios/hola", 0777), 0, NULL);
	test_check_true (file_exists ("hola/adios/hola"));

	// a directory that exists, with and without its trailing slash
	test_check_unsigned_eq (files_create_recursive_dir ("hola/adios", 0777), 0, NULL);
	test_check_unsigned_eq (files_create_recursive_dir ("hola/adios/", 0777), 0, NULL);
	test_check_unsigned_eq (files_create_recursive_dir ("hola//adios///nueva/", 0777), 0, NULL);
	test_check_true (file_exists ("hola/adios/nueva"));

	// a file in the path is not a directory
	FILE *file = fopen ("hola/archivo", "w");
	test_check_ptr (file);
	test_check_int_gt (fputs (test_string, file), -1);
	const int closed = fclose (file);
	test_check_int_eq (closed, 0, NULL);

	test_check_unsigned_eq (files_create_recursive_dir ("hola/archivo", 0777), 1, NULL);
	test_check_unsigned_eq (files_create_recursive_dir ("hola/archivo/dentro", 0777), 1, NULL);
	test_check_false (file_exists ("hola/archivo/dentro"));

	test_check_unsigned_eq (files_remove_recursive ("hola"), 0, NULL);
	test_check_false (file_exists ("hola"));

	test_check_unsigned_eq (files_create_recursive_dir (NULL, 0777), 1, NULL);
	test_check_unsigned_eq (files_create_recursive_dir ("", 0777), 1, NULL);
	test_check_unsigned_eq (files_create_recursive_dir ("/", 0777), 0, NULL);

	test_files_create_recursive_dir_long ();
	test_files_create_recursive_dir_race ();

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

// the size is the bytes that were read: a directory is opened but nothing is read from it
static void test_file_read_size (void) {

	size_t file_size = 0;
	char *file_contents = file_read ("./test/data", &file_size);

	test_check_unsigned_eq (file_size, 0, "the size of a read is not the bytes that were read");

	free (file_contents);

	file_contents = file_read ("./test/data/test.txt", NULL);
	test_check_ptr (file_contents);
	test_check_str_eq (file_contents, test_string, NULL);

	free (file_contents);

}

static void test_file_read (void) {

	test_file_read_text ();

	test_file_read_json ();

	test_file_read_size ();

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

// a missing filename or file is not an image, the bindings of python pass NULL for it
static void test_files_image_get_type_without_file (void) {

	test_check_unsigned_eq (files_image_get_type (NULL), IMAGE_TYPE_NONE, NULL);
	test_check_unsigned_eq (files_image_get_type ("/tmp/cerver-test-missing.png"), IMAGE_TYPE_NONE, NULL);
	test_check_bool_eq (file_exists (NULL), false, NULL);

}

#pragma endregion

#define FILES_HEADER_NAME			"image.png"
#define FILES_HEADER_LENGTH			256

static void files_header_set (FileHeader *file_header, const char *filename) {

	(void) memset (file_header, 0, sizeof (FileHeader));
	(void) strncpy (file_header->filename, filename, FILENAME_DEFAULT_SIZE - 1);
	file_header->len = FILES_HEADER_LENGTH;

}

// only a single, NUL terminated name is a valid filename of a FileHeader
static void test_files_header_is_valid (void) {

	FileHeader file_header;

	files_header_set (&file_header, FILES_HEADER_NAME);
	test_check_true (file_header_is_valid (&file_header));

	files_header_set (&file_header, "");
	test_check_false (file_header_is_valid (&file_header));

	files_header_set (&file_header, ".");
	test_check_false (file_header_is_valid (&file_header));

	files_header_set (&file_header, "..");
	test_check_false (file_header_is_valid (&file_header));

	files_header_set (&file_header, "../secret");
	test_check_false (file_header_is_valid (&file_header));

	files_header_set (&file_header, "/etc/passwd");
	test_check_false (file_header_is_valid (&file_header));

	files_header_set (&file_header, "directory/image.png");
	test_check_false (file_header_is_valid (&file_header));

	// a name with dots that is not a directory reference is valid
	files_header_set (&file_header, "..image.png");
	test_check_true (file_header_is_valid (&file_header));

	// a filename without its NUL would be read past the header
	(void) memset (file_header.filename, 'a', FILENAME_DEFAULT_SIZE);
	test_check_false (file_header_is_valid (&file_header));

	test_check_false (file_header_is_valid (NULL));

}

// the header is decoded from the data of a packet, that must be a complete FileHeader object
static void test_files_header_get (void) {

	Packet *packet = file_header_packet_create (
		REQUEST_PACKET_TYPE_SEND_FILE, FILES_HEADER_NAME, FILES_HEADER_LENGTH
	);

	test_check_ptr (packet);
	test_check_true ((packet->header.flags & PACKET_FLAG_SERIALIZED) != 0);

	FileHeader file_header;
	test_check_true (file_header_get (packet, &file_header));
	test_check_str_eq (file_header.filename, FILES_HEADER_NAME, NULL);
	test_check_unsigned_eq (file_header.len, FILES_HEADER_LENGTH, NULL);

	// a truncated object is not a header
	packet->data_size -= 1;
	test_check_false (file_header_get (packet, &file_header));
	packet->data_size += 1;

	// the data of a packet without the flag is not an object
	packet->header.flags = 0;
	test_check_false (file_header_get (packet, &file_header));

	test_check_false (file_header_get (NULL, &file_header));

	packet_delete (packet);

	// a name that is not valid is never decoded
	Packet *parent = file_header_packet_create (REQUEST_PACKET_TYPE_SEND_FILE, "..", 0);
	test_check_ptr (parent);
	test_check_false (file_header_get (parent, &file_header));
	packet_delete (parent);

}

#pragma region copy

#define FILES_TEST_TEMPLATE				"/tmp/cerver-files-XXXXXX"
#define FILES_TEST_TEMPLATE_SIZE		32
#define FILES_TEST_PATH_SIZE			256
#define FILES_TEST_MODE					0640
#define FILES_TEST_DIRECTORY_MODE		0750
#define FILES_TEST_PERMISSIONS			07777
#define FILES_TEST_LINK_TARGET			"a.txt"
#define FILES_TEST_BIG_SIZE				(1 << 21)
#define FILES_TEST_BYTE_MASK			0xFF

static char files_test_root[FILES_TEST_TEMPLATE_SIZE] = { 0 };

static const char *files_test_path (char path[FILES_TEST_PATH_SIZE], const char *name) {

	(void) snprintf (path, FILES_TEST_PATH_SIZE, "%s/%s", files_test_root, name);

	return path;

}

static void files_test_write (const char *path, const char *content) {

	FILE *file = fopen (path, "w");
	test_check_ptr (file);
	test_check_int_gt (fputs (content, file), -1);

	// the check prints its value again when it fails
	const int closed = fclose (file);
	test_check_int_eq (closed, 0, NULL);

}

static void files_test_check_content (const char *path, const char *expected) {

	char content[FILES_TEST_PATH_SIZE] = { 0 };

	FILE *file = fopen (path, "r");
	test_check_ptr (file);
	const size_t length = fread (content, 1, FILES_TEST_PATH_SIZE - 1, file);
	(void) fclose (file);

	test_check_unsigned_eq (length, strlen (expected), NULL);
	test_check_str_eq (content, expected, NULL);

}

static mode_t files_test_mode (const char *path) {

	struct stat st;
	test_check_int_eq (lstat (path, &st), 0, NULL);

	return st.st_mode;

}

// #323: a copy keeps the content and the permissions, and replaces the destination
static void test_files_copy (void) {

	char source[FILES_TEST_PATH_SIZE];
	char destination[FILES_TEST_PATH_SIZE];
	char missing[FILES_TEST_PATH_SIZE];

	files_test_write (files_test_path (source, "source.txt"), test_string);
	test_check_int_eq (chmod (source, FILES_TEST_MODE), 0, NULL);

	// a destination that has more bytes is replaced
	files_test_write (files_test_path (destination, "destination.txt"), sanitize_complete_three);

	test_check_unsigned_eq (files_copy (source, destination), 0, NULL);
	files_test_check_content (destination, test_string);
	test_check_unsigned_eq ((files_test_mode (destination) & FILES_TEST_PERMISSIONS), FILES_TEST_MODE, NULL);

	// a big file needs more than one range
	char big_path[FILES_TEST_PATH_SIZE];
	char big_copy[FILES_TEST_PATH_SIZE];
	FILE *big = fopen (files_test_path (big_path, "big.bin"), "w");
	test_check_ptr (big);
	for (unsigned int i = 0; i < FILES_TEST_BIG_SIZE; i++) (void) fputc ((int) (i & FILES_TEST_BYTE_MASK), big);
	(void) fclose (big);

	test_check_unsigned_eq (files_copy (big_path, files_test_path (big_copy, "big.copy")), 0, NULL);

	struct stat st;
	test_check_int_eq (stat (big_copy, &st), 0, NULL);
	test_check_unsigned_eq ((unsigned int) st.st_size, FILES_TEST_BIG_SIZE, NULL);

	// what can not be copied
	test_check_unsigned_ne (files_copy (files_test_path (missing, "missing.txt"), destination), 0);
	test_check_false (file_exists (missing));
	test_check_unsigned_ne (files_copy (files_test_root, destination), 0);
	test_check_unsigned_ne (files_copy (NULL, destination), 0);
	test_check_unsigned_ne (files_copy (source, NULL), 0);

	// a destination in a directory that does not exist
	char nowhere[FILES_TEST_PATH_SIZE];
	test_check_unsigned_ne (files_copy (source, files_test_path (nowhere, "nowhere/copy.txt")), 0);

}

// a directory is copied with its files, its directories and its links, that are not followed
static void test_files_copy_recursive (void) {

	char tree[FILES_TEST_PATH_SIZE];
	char path[FILES_TEST_PATH_SIZE];
	char copy[FILES_TEST_PATH_SIZE];

	test_check_int_eq (mkdir (files_test_path (tree, "tree"), FILES_TEST_DIRECTORY_MODE), 0, NULL);
	test_check_int_eq (mkdir (files_test_path (path, "tree/sub"), FILES_TEST_DIRECTORY_MODE), 0, NULL);
	files_test_write (files_test_path (path, "tree/a.txt"), test_string);
	files_test_write (files_test_path (path, "tree/sub/b.txt"), less_test_string);
	test_check_int_eq (symlink (FILES_TEST_LINK_TARGET, files_test_path (path, "tree/link")), 0, NULL);

	test_check_unsigned_eq (files_copy_recursive (tree, files_test_path (copy, "copy")), 0, NULL);

	files_test_check_content (files_test_path (path, "copy/a.txt"), test_string);
	files_test_check_content (files_test_path (path, "copy/sub/b.txt"), less_test_string);
	test_check_unsigned_eq ((files_test_mode (files_test_path (path, "copy/sub")) & FILES_TEST_PERMISSIONS), FILES_TEST_DIRECTORY_MODE, NULL);

	files_test_path (path, "copy/link");
	test_check_true (S_ISLNK (files_test_mode (path)));

	char target[FILES_TEST_PATH_SIZE] = { 0 };
	test_check_int_gt ((int) readlink (path, target, FILES_TEST_PATH_SIZE - 1), 0);
	test_check_str_eq (target, FILES_TEST_LINK_TARGET, NULL);

	// again into the copy that exists
	test_check_unsigned_eq (files_copy_recursive (tree, copy), 0, NULL);

	// a directory inside itself
	test_check_unsigned_ne (files_copy_recursive (tree, files_test_path (path, "tree/sub/inside")), 0);
	test_check_unsigned_ne (files_copy_recursive (tree, tree), 0);

	test_check_unsigned_ne (files_copy_recursive (files_test_path (path, "missing"), copy), 0);
	test_check_unsigned_ne (files_copy_recursive (NULL, copy), 0);

}

// the links are removed, not what they point to
static void test_files_remove_recursive (void) {

	char path[FILES_TEST_PATH_SIZE];
	char link[FILES_TEST_PATH_SIZE];

	test_check_int_eq (symlink ("../tree", files_test_path (link, "copy/tree-link")), 0, NULL);

	test_check_unsigned_eq (files_remove_recursive (files_test_path (path, "copy")), 0, NULL);
	test_check_false (file_exists (path));
	test_check_true (file_exists (files_test_path (path, "tree/sub/b.txt")));

	test_check_unsigned_eq (files_remove_recursive (files_test_path (path, "source.txt")), 0, NULL);
	test_check_false (file_exists (path));

	test_check_unsigned_ne (files_remove_recursive (files_test_path (path, "missing")), 0);
	test_check_unsigned_ne (files_remove_recursive (NULL), 0);

}

// a move renames, goes inside a directory that exists, and copies between file systems
static void test_file_move_to (void) {

	char source[FILES_TEST_PATH_SIZE];
	char destination[FILES_TEST_PATH_SIZE];
	char path[FILES_TEST_PATH_SIZE];

	files_test_write (files_test_path (source, "move.txt"), test_string);
	test_check_int_eq (file_move_to (source, files_test_path (destination, "moved.txt")), 0, NULL);
	test_check_false (file_exists (source));
	files_test_check_content (destination, test_string);

	// inside a directory
	test_check_int_eq (file_move_to (destination, files_test_path (path, "tree/sub")), 0, NULL);
	files_test_check_content (files_test_path (path, "tree/sub/moved.txt"), test_string);

	test_check_int_ne (file_move_to (files_test_path (path, "missing"), destination), 0);
	test_check_int_ne (file_move_to (NULL, destination), 0);

	// the tests run from the repository, that is in another file system than /tmp in the containers
	struct stat root;
	struct stat here;
	test_check_int_eq (stat (files_test_root, &root), 0, NULL);
	test_check_int_eq (stat (".", &here), 0, NULL);

	if (root.st_dev != here.st_dev) {
		const char *moved_tree = "moved-tree";
		(void) files_remove_recursive (moved_tree);

		test_check_int_eq (file_move_to (files_test_path (path, "tree"), moved_tree), 0, NULL);
		test_check_false (file_exists (path));
		files_test_check_content ("moved-tree/sub/b.txt", less_test_string);
		test_check_true (S_ISLNK (files_test_mode ("moved-tree/link")));

		test_check_unsigned_eq (files_remove_recursive (moved_tree), 0, NULL);
	}

	else {
		(void) printf ("The move between file systems was not tested: /tmp is in the same one\n");
	}

}

static void test_files_copy_all (void) {

	(void) snprintf (files_test_root, FILES_TEST_TEMPLATE_SIZE, "%s", FILES_TEST_TEMPLATE);
	test_check_ptr (mkdtemp (files_test_root));

	test_files_copy ();
	test_files_copy_recursive ();
	test_files_remove_recursive ();
	test_file_move_to ();

	test_check_unsigned_eq (files_remove_recursive (files_test_root), 0, NULL);
	test_check_false (file_exists (files_test_root));

}

#pragma endregion

int main (void) {

	test_output_init ();

	(void) printf ("Testing FILES...\n");

	// main
	test_files_sanitize_complete_filename ();
	test_files_create_dir ();
	test_files_create_recursive_dir ();
	test_files_get_file_extension_reference ();
	test_file_read ();
	test_file_n_read ();
	test_files_header_is_valid ();
	test_files_header_get ();

	// copy, remove and move
	test_files_copy_all ();

	// images
	test_files_image_type_to_string ();
	test_files_image_get_type_by_extension ();
	test_files_image_extension_is_jpeg ();
	test_files_image_extension_is_png ();
	test_files_image_get_type_without_file ();

	(void) printf ("\nDone with FILES tests!\n\n");

	return 0;

}
