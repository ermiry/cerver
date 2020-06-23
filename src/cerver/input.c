#include <stdio.h>
#include <stdlib.h>

#include <termios.h>

void input_clean_stdin (void) {

	int c = 0;
	do {
		c = getchar ();
	} while (c != '\n' && c != EOF);

}

// returns a newly allocated c string
char *input_get_line (void) {

	size_t lenmax = 128, len = lenmax;
	char *line = malloc (lenmax), *linep = line;
	int c = 0;

	if (line) {
		for (;;) {
			c = fgetc (stdin);

			if (c == EOF) break;

			if (--len == 0) {
				len = lenmax;
				char * linen = realloc (linep, lenmax *= 2);

				if(linen == NULL) {
					free (linep);
					return NULL;
				}

				line = linen + (line - linep);
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

	struct termios old = { 0 }, new = { 0 };

	/* Turn echoing off and fail if we can't. */
	if (!tcgetattr (fileno (stdin), &old)) {
		new = old;
		new.c_lflag &= ~ECHO;
		if (!tcsetattr (fileno (stdin), TCSAFLUSH, &new) != 0) {
			printf ("Enter password: ");
			scanf ("%128s", password);

			/* Restore terminal. */
			(void) tcsetattr (fileno (stdin), TCSAFLUSH, &old);

			retval = 0;
		}
	}

	return retval;

}