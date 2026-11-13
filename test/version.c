#include <stdlib.h>
#include <string.h>

#include <cerver/files.h>
#include <cerver/version.h>

#include "test.h"

int main (void) {

	test_output_init ();

	int retval = 1;

	// get version from file
	size_t version_len = 0;
	char *version_from_file = file_read ("version.txt", &version_len);

	if (version_from_file) {
		if (!strcmp (CERVER_VERSION, version_from_file)) {
			retval = 0;
		}

		free (version_from_file);
	}

	return retval;

}
