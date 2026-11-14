#include <stdlib.h>
#include <string.h>

#include <cerver/version.h>

#include <cerver/files/files.h>

#include "test.h"

// the version of version.h must be the one of version.txt
int main (void) {

	test_output_init ();

	(void) printf ("Testing VERSION...\n");

	// get version from file
	size_t version_len = 0;
	char *version_from_file = file_read ("version.txt", &version_len);

	test_check_ptr (version_from_file);
	test_check_str_eq (CERVER_VERSION, version_from_file, NULL);

	free (version_from_file);

	(void) printf ("\nDone with VERSION tests!\n\n");

	return 0;

}
