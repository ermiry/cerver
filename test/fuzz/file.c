#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/files.h>
#include <cerver/packets.h>

#include <cerver/types/string.h>

// a FileHeader from the network: the decode from the data of a packet, its validation,
// and the search of its filename in the paths of a files cerver
// a valid filename is a single NUL terminated name, so the search never leaves the paths

#define FUZZ_FILE_SERVED_PATH			"/tmp/cerver-fuzz-files"

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	static FileCerver *file_cerver = NULL;
	if (!file_cerver) {
		file_cerver = file_cerver_new ();
		if (!file_cerver) return 0;

		(void) file_cerver_add_path (file_cerver, FUZZ_FILE_SERVED_PATH);
	}

	// the data of a packet is copied to a buffer of its exact size, so a read past it is found
	char *packet_data = (char *) malloc (size ? size : 1);
	if (!packet_data) return 0;

	(void) memcpy (packet_data, data, size);

	Packet packet;
	(void) memset (&packet, 0, sizeof (Packet));
	packet.header.flags = PACKET_FLAG_SERIALIZED;
	packet.data = packet_data;
	packet.data_size = size;

	FileHeader file_header;
	if (file_header_get (&packet, &file_header)) {
		const size_t length = strnlen (file_header.filename, FILENAME_DEFAULT_SIZE);
		if ((length == 0) || (length == FILENAME_DEFAULT_SIZE)) abort ();
		if (strchr (file_header.filename, '/')) abort ();

		String *found = file_cerver_search_file (file_cerver, file_header.filename);
		str_delete (found);
	}

	free (packet_data);

	return 0;

}
