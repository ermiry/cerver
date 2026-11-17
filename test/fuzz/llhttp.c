#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/parser/parser.h>

#include "../conformance/outcome.h"

// the parser of cerver against llhttp 9.4.3 (the one that it replaced, built from REFERENCES), with the policy of
// cerver applied to llhttp:
// the bytes are read whole by both and in pieces by cerver, and the three outcomes must be the same,
// but for the differences that were decided (test/conformance/http.txt): a message whose head is complete
// for llhttp and that is rejected with it by cerver is only an error for both, and the error of a version
// that is not served is an error
// the first byte selects a response and the size of the pieces, the next two bytes the lenient flags

#define DIFFERENTIAL_HEADER_SIZE		3
#define DIFFERENTIAL_RESPONSE_BIT		0x80u
#define DIFFERENTIAL_PIECE_MASK			0x7Fu
#define DIFFERENTIAL_HIGH_SHIFT			8

static Outcome differential_expected;
static Outcome differential_whole;
static Outcome differential_pieces;

static const char differential_head[] = { "head " };
static const char differential_error[] = { "error" };

// a message that ends in an error is only the error: its head and its fields are left out
static void differential_normalize (Outcome *outcome) {

	char *text = outcome->text;

	// the line of the error, the bytes of a value are never at the start of a line
	char *error = strncmp (text, differential_error, sizeof (differential_error) - 1) ? NULL : text;
	for (char *line = strchr (text, '\n'); !error && line; line = strchr (line + 1, '\n')) {
		if (!strncmp (line + 1, differential_error, sizeof (differential_error) - 1)) error = line + 1;
	}

	if (!error) return;

	// the start of the line of the head of the message of the error, if it has one
	char *start = error;
	while (start > text) {
		char *line = start - 1;
		while ((line > text) && (*(line - 1) != '\n')) line--;

		if (!strncmp (line, "complete", strlen ("complete")) || !strncmp (line, "body", strlen ("body"))) break;

		start = line;
		if (!strncmp (line, differential_head, sizeof (differential_head) - 1)) break;
	}

	(void) strcpy (start, differential_error);
	outcome->length = strlen (text);

}

static void differential_print (const char *title, const Outcome *outcome) {

	(void) fprintf (stderr, "%s:\n%s\n", title, outcome->text);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < DIFFERENTIAL_HEADER_SIZE) return 0;

	const bool response = (data[0] & DIFFERENTIAL_RESPONSE_BIT) != 0;
	const size_t piece = (size_t) (data[0] & DIFFERENTIAL_PIECE_MASK) + 1;

	const unsigned int lenient = (
		(unsigned int) data[1] | ((unsigned int) data[2] << DIFFERENTIAL_HIGH_SHIFT)
	) & HTTP_PARSER_LENIENT_ALL;

	const char *text = (const char *) data + DIFFERENTIAL_HEADER_SIZE;
	const size_t length = size - DIFFERENTIAL_HEADER_SIZE;

	outcome_init (&differential_expected);
	outcome_init (&differential_whole);
	outcome_init (&differential_pieces);

	outcome_llhttp (&differential_expected, response, lenient, text, length);
	outcome_cerver (&differential_whole, response, lenient, text, length, 0);
	outcome_cerver (&differential_pieces, response, lenient, text, length, piece);

	if (differential_expected.overflow || differential_whole.overflow || differential_pieces.overflow) return 0;

	differential_normalize (&differential_expected);
	differential_normalize (&differential_whole);
	differential_normalize (&differential_pieces);

	if (
		strcmp (differential_expected.text, differential_whole.text)
		|| strcmp (differential_whole.text, differential_pieces.text)
	) {
		(void) fprintf (stderr, "response %d - pieces of %zu - lenient 0x%x\n", response, piece, lenient);
		differential_print ("llhttp", &differential_expected);
		differential_print ("cerver", &differential_whole);
		differential_print ("cerver in pieces", &differential_pieces);
		abort ();
	}

	return 0;

}
