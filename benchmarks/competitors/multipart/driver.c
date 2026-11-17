#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../measure.h"
#include "backend.h"

static const char *multipart_module = { "multipart" };

// the boundary of the bodies with its leading "--", like a browser writes it
static const char *multipart_boundary = { "------WebKitFormBoundary7MA4YWxkTrZu0gW" };

// the biggest body of the cases
#define MULTIPART_BODY_SIZE					(1 << 17)

// the file of the upload, its bytes are generated: every value of a byte, CR and LF among them
#define MULTIPART_FILE_SIZE					(1 << 16)

// the generator of the bytes of the file (a linear congruential one, the constants of Numerical Recipes)
#define MULTIPART_GENERATOR_MULTIPLIER		1664525u
#define MULTIPART_GENERATOR_INCREMENT		1013904223u
#define MULTIPART_GENERATOR_SHIFT			24
#define MULTIPART_GENERATOR_SEED			12345u

typedef struct MultipartCase {

	const char *name;
	char body[MULTIPART_BODY_SIZE];
	size_t size;

	// the parts that every implementation has to find
	int parts;

} MultipartCase;

static MultipartCase multipart_form = { "form", { 0 }, 0, 3 };
static MultipartCase multipart_upload = { "upload", { 0 }, 0, 3 };

static void multipart_append (MultipartCase *multipart_case, const char *bytes, const size_t length) {

	(void) memcpy (multipart_case->body + multipart_case->size, bytes, length);
	multipart_case->size += length;

}

static void multipart_append_text (MultipartCase *multipart_case, const char *text) {

	multipart_append (multipart_case, text, strlen (text));

}

// a part: its boundary, its headers, the empty line, its data and the CRLF before the next boundary
static void multipart_part (
	MultipartCase *multipart_case, const char *headers, const char *data, const size_t length
) {

	multipart_append_text (multipart_case, multipart_boundary);
	multipart_append_text (multipart_case, "\r\n");
	multipart_append_text (multipart_case, headers);
	multipart_append_text (multipart_case, "\r\n\r\n");
	multipart_append (multipart_case, data, length);
	multipart_append_text (multipart_case, "\r\n");

}

static void multipart_end (MultipartCase *multipart_case) {

	multipart_append_text (multipart_case, multipart_boundary);
	multipart_append_text (multipart_case, "--\r\n");

}

// a form of three fields
static void multipart_form_fill (void) {

	static const char *fields[] = { "user-123", "user123@cerver.test", "The description of the account of the user" };
	static const char *headers[] = {
		"Content-Disposition: form-data; name=\"name\"",
		"Content-Disposition: form-data; name=\"email\"",
		"Content-Disposition: form-data; name=\"description\""
	};

	for (size_t i = 0; i < (sizeof (fields) / sizeof (fields[0])); i++) {
		multipart_part (&multipart_form, headers[i], fields[i], strlen (fields[i]));
	}

	multipart_end (&multipart_form);

}

// a field, a file of 64 KiB and a JSON of its metadata
static void multipart_upload_fill (void) {

	static char file[MULTIPART_FILE_SIZE];

	uint32_t state = MULTIPART_GENERATOR_SEED;
	for (size_t i = 0; i < MULTIPART_FILE_SIZE; i++) {
		state = (state * MULTIPART_GENERATOR_MULTIPLIER) + MULTIPART_GENERATOR_INCREMENT;
		file[i] = (char) (state >> MULTIPART_GENERATOR_SHIFT);
	}

	static const char *title = { "The report of the week" };
	static const char *metadata = { "{\"pages\":12,\"author\":\"user-123\",\"tags\":[\"report\",\"week\"]}" };

	multipart_part (&multipart_upload, "Content-Disposition: form-data; name=\"title\"", title, strlen (title));
	multipart_part (
		&multipart_upload,
		"Content-Disposition: form-data; name=\"file\"; filename=\"report.bin\"\r\nContent-Type: application/octet-stream",
		file, MULTIPART_FILE_SIZE
	);
	multipart_part (
		&multipart_upload,
		"Content-Disposition: form-data; name=\"metadata\"\r\nContent-Type: application/json",
		metadata, strlen (metadata)
	);

	multipart_end (&multipart_upload);

}

static bool multipart_work (void *data) {

	const MultipartCase *multipart_case = (const MultipartCase *) data;

	return (multipart_backend_parse (multipart_boundary, multipart_case->body, multipart_case->size) == multipart_case->parts);

}

static bool multipart_measure (const MultipartCase *multipart_case) {

	const int parts = multipart_backend_parse (multipart_boundary, multipart_case->body, multipart_case->size);
	bool succeeded = (parts == multipart_case->parts);
	if (!succeeded) {
		(void) fprintf (
			stderr, "%s found %d parts in the %s body, %d expected\n",
			multipart_backend_name, parts, multipart_case->name, multipart_case->parts
		);
	}

	MeasureResult result;
	if (succeeded) {
		succeeded = measure_run (multipart_work, (void *) multipart_case, multipart_case->size, &result);
		if (succeeded) measure_print (multipart_module, multipart_case->name, multipart_backend_name, "parse", &result);
	}

	return succeeded;

}

int main (void) {

	multipart_form_fill ();
	multipart_upload_fill ();

	bool succeeded = multipart_measure (&multipart_form);
	if (succeeded) succeeded = multipart_measure (&multipart_upload);

	return succeeded ? 0 : 1;

}
