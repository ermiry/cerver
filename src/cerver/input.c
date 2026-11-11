#include <stdio.h>
#include <stdlib.h>

#include <termios.h>

#include "cerver/input.h"

void input_clean_stdin (void) {

	int c = 0;
	do {
		c = getchar ();
	} while (c != '\n' && c != EOF);

}

// returns a newly allocated c string
char *input_get_line (void) {

	size_t lenmax = 128, len = lenmax;
	char *line = (char *) malloc (lenmax), *linep = line;

	if (line) {
		int c = 0;

		for (;;) {
			c = fgetc (stdin);

			if (c == EOF || c == '\n') break;

			if (--len == 0) {
				len = lenmax;

				// the offset is taken before realloc () frees the old buffer
				const size_t offset = (size_t) (line - linep);
				char * linen = (char *) realloc (linep, lenmax *= 2);

				if (linen == NULL) {
					free (linep);
					return NULL;
				}

				line = linen + offset;
				linep = linen;
			}

			if ((*line++ = c) == '\n') break;
		}

		*line = '\0';
	}

	return linep;

}

unsigned int input_password (char *password) {

	unsigned int retval = 1;

	struct termios old_term = CERVER_ZERO_INITIALIZER, new_term = CERVER_ZERO_INITIALIZER;

	/* Turn echoing off and fail if we can't. */
	if (!tcgetattr (fileno (stdin), &old_term)) {
		new_term = old_term;
		new_term.c_lflag &= ~ECHO;
		if (!tcsetattr (fileno (stdin), TCSAFLUSH, &new_term)) {
			// the password buffer holds INPUT_PASSWORD_SIZE bytes, with its NUL
			const int read = scanf (INPUT_PASSWORD_SCAN_FORMAT, password);

			/* Restore terminal. */
			(void) tcsetattr (fileno (stdin), TCSAFLUSH, &old_term);

			if (read == 1) retval = 0;
		}
	}

	return retval;

}
