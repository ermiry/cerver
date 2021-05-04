#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/content.h>

#include "../test.h"

static const char *content_type_text_string = { "html" };
static const char *content_type_undefined_string = { "undefined" };
static const char *content_type_css_string = { "css" };
static const char *content_type_js_string = { "js" };
static const char *content_type_json_string = { "json" };
static const char *content_type_octet_string = { "octet" };
static const char *content_type_jpg_string = { "jpg" };
static const char *content_type_png_string = { "png" };
static const char *content_type_ico_string = { "ico" };
static const char *content_type_gif_string = { "gif" };
static const char *content_type_audio_string = { "mp3" };

static const char *content_type_undefined_description = { "undefined" };
static const char *content_type_text_description = { "text/html; charset=UTF-8" };
static const char *content_type_css_description = { "text/css" };
static const char *content_type_js_description = { "application/javascript" };
static const char *content_type_json_description = { "application/json" };
static const char *content_type_octet_description = { "application/octet-stream" };
static const char *content_type_jpg_description = { "image/jpg" };
static const char *content_type_png_description = { "image/png" };
static const char *content_type_ico_description = { "image/x-icon" };
static const char *content_type_gif_description = { "image/gif" };
static const char *content_type_audio_description = { "audio/mp3" };

static void test_http_content_type_string (void) {

	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_NONE), content_type_undefined_string, NULL);

	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_HTML), content_type_text_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_CSS), content_type_css_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_JS), content_type_js_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_JSON), content_type_json_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_BIN), content_type_octet_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_JPG), content_type_jpg_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_PNG), content_type_png_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_ICO), content_type_ico_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_GIF), content_type_gif_string, NULL);
	test_check_str_eq (http_content_type_extension (HTTP_CONTENT_TYPE_MP3), content_type_audio_string, NULL);

	test_check_null_ptr (http_content_type_extension ((const ContentType) 100));

}

static void test_http_content_type_description (void) {

	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_NONE), content_type_undefined_description, NULL);

	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_HTML), content_type_text_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_CSS), content_type_css_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_JS), content_type_js_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_JSON), content_type_json_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_BIN), content_type_octet_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_JPG), content_type_jpg_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_PNG), content_type_png_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_ICO), content_type_ico_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_GIF), content_type_gif_description, NULL);
	test_check_str_eq (http_content_type_description (HTTP_CONTENT_TYPE_MP3), content_type_audio_description, NULL);

	test_check_null_ptr (http_content_type_description ((const ContentType) 100));

}

static void test_http_content_type_by_string (void) {

	test_check_unsigned_eq (http_content_type_by_mime (content_type_text_description), HTTP_CONTENT_TYPE_HTML, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_css_description), HTTP_CONTENT_TYPE_CSS, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_js_description), HTTP_CONTENT_TYPE_JS, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_json_description), HTTP_CONTENT_TYPE_JSON, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_octet_description), HTTP_CONTENT_TYPE_BIN, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_jpg_description), HTTP_CONTENT_TYPE_JPG, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_png_description), HTTP_CONTENT_TYPE_PNG, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_ico_description), HTTP_CONTENT_TYPE_ICO, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_gif_description), HTTP_CONTENT_TYPE_GIF, NULL);
	test_check_unsigned_eq (http_content_type_by_mime (content_type_audio_description), HTTP_CONTENT_TYPE_MP3, NULL);

}

static void test_http_content_type_by_extension (void) {

	test_check_str_eq (http_content_type_mime_by_extension (content_type_text_string), content_type_text_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_css_string), content_type_css_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_js_string), content_type_js_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_json_string), content_type_json_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_octet_string), content_type_octet_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_jpg_string), content_type_jpg_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_png_string), content_type_png_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_ico_string), content_type_ico_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_gif_string), content_type_gif_description, NULL);
	test_check_str_eq (http_content_type_mime_by_extension (content_type_audio_string), content_type_audio_description, NULL);

}

static void test_http_content_type_is_json (void) {

	test_check_bool_eq (http_content_type_is_json (content_type_text_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_css_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_js_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_json_description), true, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_octet_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_jpg_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_png_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_ico_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_gif_description), false, NULL);
	test_check_bool_eq (http_content_type_is_json (content_type_audio_description), false, NULL);

}

void http_tests_contents (void) {

	(void) printf ("Testing HTTP contents...\n");

	test_http_content_type_string ();
	test_http_content_type_description ();
	test_http_content_type_by_string ();
	test_http_content_type_by_extension ();
	test_http_content_type_is_json ();

	(void) printf ("Done!\n");

}
