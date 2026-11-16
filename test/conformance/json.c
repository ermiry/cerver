#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/json/document.h>
#include <cerver/json/events.h>
#include <cerver/json/json.h>

// runs the cases of a suite of JSON parsing (JSONTestSuite): every file of a directory is a text whose name
// says what a parser must do with it, y_ to take it, n_ to reject it and i_ for the ones that a parser decides;
// the decisions of cerver and how many cases of each kind the suite has are in a file of the repository
// usage: test/bin/conformance/json <directory of the cases> <file of the decisions>

#define CONFORMANCE_PATH_SIZE				(1 << 10)
#define CONFORMANCE_LINE_SIZE				(1 << 10)
#define CONFORMANCE_NAME_SIZE				(1 << 8)

// the most decisions that the file of the repository can have
#define CONFORMANCE_DECISIONS_SIZE			(1 << 8)

// the arguments of the program: itself, the directory and the file; a line of the file has as many fields
#define CONFORMANCE_ARGUMENTS				3

#define CONFORMANCE_BASE					10

static const char CONFORMANCE_EXTENSION[] = ".json";

static const char CONFORMANCE_ACCEPT[] = "accept";
static const char CONFORMANCE_REJECT[] = "reject";
static const char CONFORMANCE_SUITE[] = "suite";

// the kinds of cases, by the first letter of the name of their files
typedef enum ConformanceKind {

	CONFORMANCE_KIND_ACCEPT,
	CONFORMANCE_KIND_REJECT,
	CONFORMANCE_KIND_DECIDED,
	CONFORMANCE_KIND_COUNT

} ConformanceKind;

static const char conformance_kind_letters[CONFORMANCE_KIND_COUNT] = { 'y', 'n', 'i' };

// what cerver does with a case that a parser decides
typedef struct ConformanceDecision {

	char name[CONFORMANCE_NAME_SIZE];
	bool accept;
	bool used;

} ConformanceDecision;

// the decisions of the repository and how many cases of each kind the suite has
typedef struct ConformanceDecisions {

	ConformanceDecision decisions[CONFORMANCE_DECISIONS_SIZE];
	size_t count;

	size_t expected[CONFORMANCE_KIND_COUNT];

} ConformanceDecisions;

// what a run found
typedef struct ConformanceResults {

	size_t cases[CONFORMANCE_KIND_COUNT];
	size_t failed;

} ConformanceResults;

#pragma region decisions

static bool conformance_kind_of (const char letter, ConformanceKind *kind) {

	for (size_t i = 0; i < CONFORMANCE_KIND_COUNT; i++) {
		if (conformance_kind_letters[i] == letter) {
			*kind = (ConformanceKind) i;
			return true;
		}
	}

	return false;

}

// a line of the file: "suite <letter> <cases>", or "accept <file> <why>" and "reject <file> <why>"
static bool conformance_decisions_line (ConformanceDecisions *decisions, const char *line) {

	char verb[CONFORMANCE_NAME_SIZE] = { 0 };
	char name[CONFORMANCE_NAME_SIZE] = { 0 };
	char rest[CONFORMANCE_LINE_SIZE] = { 0 };

	const int fields = sscanf (line, "%255s %255s %1023[^\n]", verb, name, rest);
	if (fields <= 0) return true;

	if (fields < CONFORMANCE_ARGUMENTS) return false;

	if (!strcmp (verb, CONFORMANCE_SUITE)) {
		ConformanceKind kind = CONFORMANCE_KIND_ACCEPT;
		if (!conformance_kind_of (name[0], &kind)) return false;

		decisions->expected[kind] = strtoul (rest, NULL, CONFORMANCE_BASE);
		return true;
	}

	const bool accept = !strcmp (verb, CONFORMANCE_ACCEPT);
	if (!accept && strcmp (verb, CONFORMANCE_REJECT)) return false;

	if (decisions->count >= CONFORMANCE_DECISIONS_SIZE) return false;

	ConformanceDecision *decision = &decisions->decisions[decisions->count];
	(void) snprintf (decision->name, sizeof (decision->name), "%s", name);
	decision->accept = accept;
	decision->used = false;

	decisions->count += 1;

	return true;

}

static bool conformance_decisions_load (ConformanceDecisions *decisions, const char *path) {

	bool retval = false;

	char line[CONFORMANCE_LINE_SIZE];

	FILE *file = fopen (path, "r");
	if (file) {
		retval = true;

		while (retval && fgets (line, sizeof (line), file)) {
			retval = conformance_decisions_line (decisions, line);
			if (!retval) (void) fprintf (stderr, "The line <%s> of %s is not a decision\n", line, path);
		}

		(void) fclose (file);
	}

	else {
		(void) fprintf (stderr, "The file %s could not be opened\n", path);
	}

	return retval;

}

static ConformanceDecision *conformance_decisions_find (ConformanceDecisions *decisions, const char *name) {

	for (size_t i = 0; i < decisions->count; i++) {
		if (!strcmp (decisions->decisions[i].name, name)) return &decisions->decisions[i];
	}

	return NULL;

}

#pragma endregion

#pragma region cases

// the whole content of a file, with the padding that a text read in situ needs
static char *conformance_file_read (const char *path, size_t *length) {

	char *content = NULL;

	FILE *file = fopen (path, "rb");
	if (file) {
		if (!fseek (file, 0, SEEK_END)) {
			const long size = ftell (file);
			if ((size >= 0) && !fseek (file, 0, SEEK_SET)) {
				content = (char *) malloc ((size_t) size + CERVER_JSON_PADDING);
				if (content && (fread (content, 1, (size_t) size, file) != (size_t) size)) {
					free (content);
					content = NULL;
				}

				*length = (size_t) size;
			}
		}

		(void) fclose (file);
	}

	return content;

}

// a text with every reader: a document and the events, from a copy and in situ; they must all agree
static bool conformance_case_accepts (const char *content, const size_t length, bool *accepted) {

	static const CerverJsonEvents events = { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL };

	bool retval = false;

	char *copy = (char *) malloc (length + CERVER_JSON_PADDING);
	char *other = (char *) malloc (length + CERVER_JSON_PADDING);

	if (copy && other) {
		(void) memcpy (copy, content, length);
		(void) memcpy (other, content, length);

		CerverJsonDocument *document = cerver_json_parse (NULL, content, length, CERVER_JSON_FLAGS_NONE, NULL, NULL);
		CerverJsonDocument *in_situ = cerver_json_parse_in_situ (NULL, copy, length, CERVER_JSON_FLAGS_NONE, NULL, NULL);

		const bool read = cerver_json_read (NULL, content, length, CERVER_JSON_FLAGS_NONE, NULL, &events, NULL, NULL);
		const bool read_in_situ = cerver_json_read_in_situ (
			NULL, other, length, CERVER_JSON_FLAGS_NONE, NULL, &events, NULL, NULL
		);

		*accepted = (document != NULL);

		retval = ((in_situ != NULL) == *accepted) && (read == *accepted) && (read_in_situ == *accepted);

		cerver_json_document_delete (document);
		cerver_json_document_delete (in_situ);
	}

	free (copy);
	free (other);

	return retval;

}

// what a case expects; false for a case that a parser decides without a decision in the repository
static bool conformance_case_expected (
	ConformanceDecisions *decisions, const ConformanceKind kind, const char *name, bool *accept
) {

	if (kind != CONFORMANCE_KIND_DECIDED) {
		*accept = (kind == CONFORMANCE_KIND_ACCEPT);
		return true;
	}

	ConformanceDecision *decision = conformance_decisions_find (decisions, name);
	if (!decision) return false;

	decision->used = true;
	*accept = decision->accept;

	return true;

}

static bool conformance_case_run (ConformanceDecisions *decisions, const char *directory, const char *name) {

	char path[CONFORMANCE_PATH_SIZE];

	ConformanceKind kind = CONFORMANCE_KIND_ACCEPT;
	bool expected = false;
	bool accepted = false;
	size_t length = 0;

	(void) conformance_kind_of (name[0], &kind);

	if (!conformance_case_expected (decisions, kind, name, &expected)) {
		(void) fprintf (stderr, "%s has no decision\n", name);
		return false;
	}

	(void) snprintf (path, sizeof (path), "%s/%s", directory, name);

	char *content = conformance_file_read (path, &length);
	if (!content) {
		(void) fprintf (stderr, "%s could not be read\n", name);
		return false;
	}

	bool retval = conformance_case_accepts (content, length, &accepted);
	if (!retval) {
		(void) fprintf (stderr, "%s: the readers do not agree\n", name);
	}

	else if (accepted != expected) {
		(void) fprintf (stderr, "%s was %s\n", name, accepted ? "accepted" : "rejected");
		retval = false;
	}

	free (content);

	return retval;

}

// a file of the suite: its name starts with the letter of a kind and ends with the extension
static bool conformance_case_is (const char *name, ConformanceKind *kind) {

	const size_t length = strlen (name);
	const size_t extension = sizeof (CONFORMANCE_EXTENSION) - 1;

	return (length > extension)
		&& !strcmp (name + length - extension, CONFORMANCE_EXTENSION)
		&& (name[1] == '_')
		&& conformance_kind_of (name[0], kind);

}

static bool conformance_run (ConformanceDecisions *decisions, const char *path, ConformanceResults *results) {

	DIR *directory = opendir (path);
	if (!directory) {
		(void) fprintf (stderr, "The directory %s could not be opened\n", path);
		return false;
	}

	for (struct dirent *entry = readdir (directory); entry; entry = readdir (directory)) {
		ConformanceKind kind = CONFORMANCE_KIND_ACCEPT;

		if (conformance_case_is (entry->d_name, &kind)) {
			results->cases[kind] += 1;

			if (!conformance_case_run (decisions, path, entry->d_name)) results->failed += 1;
		}
	}

	(void) closedir (directory);

	return true;

}

#pragma endregion

// the suite must have the cases that the decisions were written for, and every decision its case
static bool conformance_check (const ConformanceDecisions *decisions, const ConformanceResults *results) {

	bool retval = !results->failed;

	for (size_t i = 0; i < CONFORMANCE_KIND_COUNT; i++) {
		(void) printf (
			"%c_ cases: %zu of %zu\n", conformance_kind_letters[i], results->cases[i], decisions->expected[i]
		);

		if (results->cases[i] != decisions->expected[i]) retval = false;
	}

	for (size_t i = 0; i < decisions->count; i++) {
		if (!decisions->decisions[i].used) {
			(void) fprintf (stderr, "The decision of %s has no case\n", decisions->decisions[i].name);
			retval = false;
		}
	}

	(void) printf ("Failed cases: %zu\n", results->failed);

	return retval;

}

int main (int argc, char **argv) {

	static ConformanceDecisions decisions;
	ConformanceResults results = { { 0 }, 0 };

	if (argc != CONFORMANCE_ARGUMENTS) {
		(void) fprintf (stderr, "Usage: %s <directory of the cases> <file of the decisions>\n", argv[0]);
		return 1;
	}

	if (!conformance_decisions_load (&decisions, argv[2]) || !conformance_run (&decisions, argv[1], &results)) {
		return 1;
	}

	return conformance_check (&decisions, &results) ? 0 : 1;

}
