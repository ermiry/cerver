#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <multipart_parser.h>

#include <cerver/http/parser/multipart.h>

// the parser of the multipart bodies of cerver against multipart-parser-c, the one that it replaced:
// every callback with its bytes and the bytes that each one read must be the same, for the body whole
// and in pieces (each piece is a call of its own for both parsers)
// the first byte is the size of the pieces, the second one the length of the boundary, then the boundary and
// the body

#define PARTS_HEADER_SIZE			2
#define PARTS_LOG_SIZE				(1 << 17)

typedef struct PartsLog {

	char text[PARTS_LOG_SIZE];
	size_t length;
	bool overflow;

} PartsLog;

static PartsLog parts_expected;
static PartsLog parts_found;

static void parts_log (PartsLog *log, const char kind, const char *at, const size_t length) {

	if (log->overflow || ((log->length + length + 2) >= PARTS_LOG_SIZE)) {
		log->overflow = true;
		return;
	}

	log->text[log->length++] = kind;
	if (length) (void) memcpy (log->text + log->length, at, length);
	log->length += length;
	log->text[log->length++] = '|';

}

#pragma region reference

static void parts_reference_log (multipart_parser *parser, const char kind, const char *at, const size_t length) {

	parts_log ((PartsLog *) multipart_parser_get_data (parser), kind, at, length);

}

static int parts_reference_field (multipart_parser *parser, const char *at, size_t length) {

	parts_reference_log (parser, 'f', at, length);
	return 0;

}

static int parts_reference_value (multipart_parser *parser, const char *at, size_t length) {

	parts_reference_log (parser, 'v', at, length);
	return 0;

}

static int parts_reference_data (multipart_parser *parser, const char *at, size_t length) {

	parts_reference_log (parser, 'd', at, length);
	return 0;

}

static int parts_reference_begin (multipart_parser *parser) {

	parts_reference_log (parser, 'b', NULL, 0);
	return 0;

}

static int parts_reference_headers (multipart_parser *parser) {

	parts_reference_log (parser, 'h', NULL, 0);
	return 0;

}

static int parts_reference_end (multipart_parser *parser) {

	parts_reference_log (parser, 'e', NULL, 0);
	return 0;

}

static int parts_reference_body_end (multipart_parser *parser) {

	parts_reference_log (parser, 'z', NULL, 0);
	return 0;

}

static const multipart_parser_settings parts_reference_settings = {
	parts_reference_field, parts_reference_value, parts_reference_data,
	parts_reference_begin, parts_reference_headers, parts_reference_end, parts_reference_body_end
};

#pragma endregion

#pragma region cerver

static int parts_cerver_field (CerverHttpMultipart *parser, const char *at, size_t length) {

	parts_log ((PartsLog *) parser->data, 'f', at, length);
	return 0;

}

static int parts_cerver_value (CerverHttpMultipart *parser, const char *at, size_t length) {

	parts_log ((PartsLog *) parser->data, 'v', at, length);
	return 0;

}

static int parts_cerver_data (CerverHttpMultipart *parser, const char *at, size_t length) {

	parts_log ((PartsLog *) parser->data, 'd', at, length);
	return 0;

}

static int parts_cerver_begin (CerverHttpMultipart *parser) {

	parts_log ((PartsLog *) parser->data, 'b', NULL, 0);
	return 0;

}

static int parts_cerver_headers (CerverHttpMultipart *parser) {

	parts_log ((PartsLog *) parser->data, 'h', NULL, 0);
	return 0;

}

static int parts_cerver_end (CerverHttpMultipart *parser) {

	parts_log ((PartsLog *) parser->data, 'e', NULL, 0);
	return 0;

}

static int parts_cerver_body_end (CerverHttpMultipart *parser) {

	parts_log ((PartsLog *) parser->data, 'z', NULL, 0);
	return 0;

}

static const CerverHttpMultipartCallbacks parts_cerver_callbacks = {
	parts_cerver_field, parts_cerver_value, parts_cerver_data,
	parts_cerver_begin, parts_cerver_headers, parts_cerver_end, parts_cerver_body_end
};

#pragma endregion

static void parts_compare (void) {

	if (parts_expected.overflow || parts_found.overflow) return;

	if (
		(parts_expected.length != parts_found.length)
		|| memcmp (parts_expected.text, parts_found.text, parts_found.length)
	) {
		(void) fprintf (
			stderr, "multipart-parser-c:\n%.*s\ncerver:\n%.*s\n",
			(int) parts_expected.length, parts_expected.text, (int) parts_found.length, parts_found.text
		);

		abort ();
	}

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < PARTS_HEADER_SIZE) return 0;

	const size_t piece = (size_t) data[0] + 1;
	const size_t boundary_length = (size_t) data[1];
	if (!boundary_length || ((PARTS_HEADER_SIZE + boundary_length) > size)) return 0;
	if (boundary_length >= CERVER_HTTP_MULTIPART_BOUNDARY_SIZE) return 0;

	// multipart-parser-c takes the boundary as a C string
	char boundary[CERVER_HTTP_MULTIPART_BOUNDARY_SIZE];
	(void) memcpy (boundary, data + PARTS_HEADER_SIZE, boundary_length);
	boundary[boundary_length] = '\0';
	if (strlen (boundary) != boundary_length) return 0;

	const char *body = (const char *) data + PARTS_HEADER_SIZE + boundary_length;
	const size_t length = size - PARTS_HEADER_SIZE - boundary_length;

	(void) memset (&parts_expected, 0, sizeof (PartsLog));
	(void) memset (&parts_found, 0, sizeof (PartsLog));

	multipart_parser *reference = multipart_parser_init (boundary, &parts_reference_settings);
	if (!reference) return 0;
	multipart_parser_set_data (reference, &parts_expected);

	static CerverHttpMultipart multipart;
	if (!cerver_http_multipart_init (&multipart, boundary, boundary_length, &parts_cerver_callbacks, &parts_found)) abort ();

	for (size_t offset = 0; offset < length; offset += piece) {
		const size_t size_of_piece = ((length - offset) < piece) ? (length - offset) : piece;

		const size_t reference_read = multipart_parser_execute (reference, body + offset, size_of_piece);
		const size_t cerver_read = cerver_http_multipart_execute (&multipart, body + offset, size_of_piece);

		if (reference_read != cerver_read) {
			(void) fprintf (
				stderr, "multipart-parser-c read %zu bytes and cerver %zu of a piece of %zu\n",
				reference_read, cerver_read, size_of_piece
			);

			abort ();
		}

		if (reference_read != size_of_piece) break;
	}

	multipart_parser_free (reference);

	parts_compare ();

	return 0;

}
