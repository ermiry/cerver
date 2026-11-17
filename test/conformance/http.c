#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/parser/parser.h>

#include "outcome.h"

// the conformance of the HTTP parser against the tests of llhttp (test/request/*.md and test/response/*.md of its
// tag, downloaded by Dockerfile.conformance): each case is read by the copy of llhttp of the tree and by the parser
// of cerver, whole and in pieces of one byte, and the three outcomes must be the same;
// test/conformance/http.txt has the verdict of each case where they are not (a decided difference)
// usage: http <verdicts> <file.md>...

#define CONFORMANCE_LINE_SIZE			(1 << 14)
#define CONFORMANCE_TEXT_SIZE			(1 << 17)
#define CONFORMANCE_NAME_SIZE			(1 << 9)
#define CONFORMANCE_TYPE_SIZE			(1 << 7)
#define CONFORMANCE_VERDICTS			(1 << 9)

#define CONFORMANCE_HEX_BASE			16
#define CONFORMANCE_OCTAL_BASE			8
#define CONFORMANCE_OCTAL_DIGITS		3

typedef struct ConformanceLenient {

	const char *suffix;
	unsigned int flags;

} ConformanceLenient;

static const ConformanceLenient conformance_lenients[] = {
	{ "-lenient-all", HTTP_PARSER_LENIENT_ALL },
	{ "-lenient-headers", HTTP_PARSER_LENIENT_HEADERS },
	{ "-lenient-chunked-length", HTTP_PARSER_LENIENT_CHUNKED_LENGTH },
	{ "-lenient-keep-alive", HTTP_PARSER_LENIENT_KEEP_ALIVE },
	{ "-lenient-transfer-encoding", HTTP_PARSER_LENIENT_TRANSFER_ENCODING },
	{ "-lenient-version", HTTP_PARSER_LENIENT_VERSION },
	{ "-lenient-data-after-close", HTTP_PARSER_LENIENT_DATA_AFTER_CLOSE },
	{ "-lenient-optional-lf-after-cr", HTTP_PARSER_LENIENT_OPTIONAL_LF_AFTER_CR },
	{ "-lenient-optional-crlf-after-chunk", HTTP_PARSER_LENIENT_OPTIONAL_CRLF_AFTER_CHUNK },
	{ "-lenient-optional-cr-before-lf", HTTP_PARSER_LENIENT_OPTIONAL_CR_BEFORE_LF },
	{ "-lenient-spaces-after-chunk-size", HTTP_PARSER_LENIENT_SPACES_AFTER_CHUNK_SIZE }
};

#define CONFORMANCE_LENIENTS		(sizeof (conformance_lenients) / sizeof (conformance_lenients[0]))

// the flag that the copy of llhttp does not have
static const char conformance_relaxed[] = { "-lenient-header-value-relaxed" };

static const char conformance_response[] = { "response" };

typedef struct ConformanceVerdict {

	char key[CONFORMANCE_NAME_SIZE];
	char verdict[CONFORMANCE_NAME_SIZE];
	bool used;

} ConformanceVerdict;

static ConformanceVerdict conformance_verdicts[CONFORMANCE_VERDICTS];
static size_t conformance_verdicts_count = 0;

static unsigned int conformance_cases = 0;
static unsigned int conformance_same = 0;
static unsigned int conformance_decided = 0;
static unsigned int conformance_skipped = 0;
static unsigned int conformance_failed = 0;

static Outcome conformance_expected;
static Outcome conformance_whole;
static Outcome conformance_pieces;

#pragma region verdicts

// a line is "<file>#<case>\t<verdict>", the lines that start with # are comments
static void conformance_verdicts_read (const char *path) {

	FILE *file = fopen (path, "r");
	if (!file) {
		(void) fprintf (stderr, "the verdicts %s can not be read\n", path);
		exit (1);
	}

	char line[CONFORMANCE_LINE_SIZE];
	while (fgets (line, sizeof (line), file) && (conformance_verdicts_count < CONFORMANCE_VERDICTS)) {
		line[strcspn (line, "\n")] = '\0';
		if ((line[0] == '#') || !line[0]) continue;

		char *tab = strchr (line, '\t');
		if (!tab) continue;
		*tab = '\0';

		ConformanceVerdict *verdict = &conformance_verdicts[conformance_verdicts_count++];
		(void) snprintf (verdict->key, sizeof (verdict->key), "%.*s", (int) (sizeof (verdict->key) - 1), line);
		(void) snprintf (verdict->verdict, sizeof (verdict->verdict), "%.*s", (int) (sizeof (verdict->verdict) - 1), tab + 1);
	}

	(void) fclose (file);

}

static ConformanceVerdict *conformance_verdict_find (const char *key) {

	ConformanceVerdict *found = NULL;
	for (size_t i = 0; !found && (i < conformance_verdicts_count); i++) {
		if (!strcmp (conformance_verdicts[i].key, key)) found = &conformance_verdicts[i];
	}

	return found;

}

#pragma endregion

#pragma region input

static int conformance_hex_value (const char digit) {

	int value = -1;
	if ((digit >= '0') && (digit <= '9')) value = digit - '0';
	else if ((digit >= 'a') && (digit <= 'f')) value = digit - 'a' + 10;
	else if ((digit >= 'A') && (digit <= 'F')) value = digit - 'A' + 10;

	return value;

}

// an escape of md-test.ts: \r, \n, \t, \f, \xHH... and \ooo
static size_t conformance_escape (const char *source, const size_t length, size_t i, char *byte) {

	const char escaped = source[i + 1];
	size_t used = 2;

	switch (escaped) {
		case 'r': *byte = '\r'; break;
		case 'n': *byte = '\n'; break;
		case 't': *byte = '\t'; break;
		case 'f': *byte = '\f'; break;

		case 'x': {
			unsigned int value = 0;
			size_t j = i + 2;
			while ((j < length) && (conformance_hex_value (source[j]) >= 0)) {
				value = (value * CONFORMANCE_HEX_BASE) + (unsigned int) conformance_hex_value (source[j]);
				j++;
			}

			*byte = (char) value;
			used = j - i;
		} break;

		default:
			if ((escaped >= '0') && (escaped <= '7')) {
				unsigned int value = 0;
				size_t j = i + 1;
				while ((j < length) && (j < (i + 1 + CONFORMANCE_OCTAL_DIGITS)) && (source[j] >= '0') && (source[j] <= '7')) {
					value = (value * CONFORMANCE_OCTAL_BASE) + (unsigned int) (source[j] - '0');
					j++;
				}

				*byte = (char) value;
				used = j - i;
			}

			else {
				*byte = '\\';
				used = 1;
			}
			break;
	}

	return used;

}

// the text of a block as md-test.ts makes it: without its last line break, without the line breaks after a
// backslash, with CRLF for every other line break and with its escapes
static size_t conformance_input (const char *block, size_t length, char *text) {

	if (length && (block[length - 1] == '\n')) length -= 1;

	size_t written = 0;
	for (size_t i = 0; i < length; i++) {
		if ((block[i] == '\\') && ((i + 1) < length) && (block[i + 1] == '\n')) {
			i += 1;
		}

		else if (block[i] == '\n') {
			text[written++] = '\r';
			text[written++] = '\n';
		}

		else if ((block[i] == '\\') && ((i + 1) < length)) {
			char byte = 0;
			i += conformance_escape (block, length, i, &byte) - 1;
			text[written++] = byte;
		}

		else {
			text[written++] = block[i];
		}
	}

	return written;

}

#pragma endregion

#pragma region cases

static void conformance_print (const char *title, const Outcome *outcome) {

	(void) fprintf (stdout, "  %s:\n%s", title, outcome->text);

}

static void conformance_case (
	const char *key, const char *type, const char *text, const size_t length
) {

	conformance_cases += 1;

	ConformanceVerdict *verdict = conformance_verdict_find (key);
	if (verdict) verdict->used = true;

	if (strstr (type, conformance_relaxed) || (verdict && !strncmp (verdict->verdict, "skip", 4))) {
		conformance_skipped += 1;
		return;
	}

	const bool response = !strncmp (type, conformance_response, sizeof (conformance_response) - 1);

	unsigned int lenient = 0;
	for (size_t i = 0; i < CONFORMANCE_LENIENTS; i++) {
		if (strstr (type, conformance_lenients[i].suffix)) lenient |= conformance_lenients[i].flags;
	}

	outcome_init (&conformance_expected);
	outcome_init (&conformance_whole);
	outcome_init (&conformance_pieces);

	outcome_llhttp (&conformance_expected, response, lenient, text, length);
	outcome_cerver (&conformance_whole, response, lenient, text, length, 0);
	outcome_cerver (&conformance_pieces, response, lenient, text, length, 1);

	const bool same = !strcmp (conformance_expected.text, conformance_whole.text)
		&& !strcmp (conformance_whole.text, conformance_pieces.text);

	if (same && !verdict) {
		conformance_same += 1;
	}

	else if (!same && verdict && !strcmp (conformance_whole.text, conformance_pieces.text)) {
		conformance_decided += 1;
	}

	else {
		conformance_failed += 1;
		(void) fprintf (stdout, "FAILED %s (%s)%s\n", key, type, (same && verdict) ? " - the same with a verdict" : "");
		conformance_print ("llhttp", &conformance_expected);
		conformance_print ("cerver", &conformance_whole);
		conformance_print ("cerver in pieces", &conformance_pieces);
	}

}

static const char *conformance_file_name (const char *path) {

	const char *slash = strrchr (path, '/');
	const char *folder = path;

	// the folder and the file: request/sample.md
	if (slash) {
		folder = slash;
		while ((folder > path) && (*(folder - 1) != '/')) folder--;
	}

	return folder;

}

// the type of a meta comment: <!-- meta={"type": "request", ...} -->
static bool conformance_meta_type (const char *line, char *type) {

	const char *key = strstr (line, "\"type\"");
	if (!key) return false;

	const char *start = strchr (key + strlen ("\"type\""), '"');
	if (!start) return false;
	start += 1;

	const char *end = strchr (start, '"');
	if (!end) return false;

	(void) snprintf (type, CONFORMANCE_TYPE_SIZE, "%.*s", (int) (end - start), start);

	return true;

}

static void conformance_file (const char *path) {

	FILE *file = fopen (path, "r");
	if (!file) {
		(void) fprintf (stderr, "%s can not be read\n", path);
		exit (1);
	}

	static char block[CONFORMANCE_TEXT_SIZE];
	static char text[CONFORMANCE_TEXT_SIZE * 2];

	char line[CONFORMANCE_LINE_SIZE];
	char name[CONFORMANCE_NAME_SIZE] = { 0 };
	char type[CONFORMANCE_TYPE_SIZE] = { 0 };
	char key[CONFORMANCE_NAME_SIZE * 2];

	bool in_block = false;
	size_t block_length = 0;

	while (fgets (line, sizeof (line), file)) {
		if (in_block) {
			if (!strncmp (line, "```", 3)) {
				in_block = false;

				const size_t length = conformance_input (block, block_length, text);
				(void) snprintf (key, sizeof (key), "%s#%s", conformance_file_name (path), name);
				if (type[0]) conformance_case (key, type, text, length);
				type[0] = '\0';
			}

			else if ((block_length + strlen (line)) < sizeof (block)) {
				(void) memcpy (block + block_length, line, strlen (line));
				block_length += strlen (line);
			}
		}

		else if (!strncmp (line, "##", 2)) {
			const char *title = line + strspn (line, "# ");
			(void) snprintf (name, sizeof (name), "%.*s", (int) (sizeof (name) - 1), title);
			name[strcspn (name, "\n")] = '\0';
		}

		else if (strstr (line, "<!-- meta=")) {
			if (!conformance_meta_type (line, type)) type[0] = '\0';
			if (strstr (line, "\"skip\": true")) type[0] = '\0';
		}

		else if (!strncmp (line, "```http", 7)) {
			in_block = true;
			block_length = 0;
		}
	}

	(void) fclose (file);

}

#pragma endregion

int main (int argc, char **argv) {

	if (argc < 3) {
		(void) fprintf (stderr, "usage: %s <verdicts> <file.md>...\n", argv[0]);
		return 1;
	}

	conformance_verdicts_read (argv[1]);

	for (int i = 2; i < argc; i++) conformance_file (argv[i]);

	for (size_t i = 0; i < conformance_verdicts_count; i++) {
		if (!conformance_verdicts[i].used) {
			(void) fprintf (stdout, "UNUSED verdict %s\n", conformance_verdicts[i].key);
			conformance_failed += 1;
		}
	}

	(void) fprintf (
		stdout, "cases %u - the same %u - decided %u - skipped %u - failed %u\n",
		conformance_cases, conformance_same, conformance_decided, conformance_skipped, conformance_failed
	);

	return conformance_failed ? 1 : 0;

}
