#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/packets.h>
#include <cerver/timer.h>

#define BUFFER_SIZE				1024

#define APP_REQUEST_MESSAGE		10

/* 8 gb */
static const int64_t kBytes = 8LL << 30;

static const char *MESSAGE = {
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
	"incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud "
	"exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat. "
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor "
	"incididunt ut labore et dolore magna aliqua."
};

int main (int argc, char **argv) {

	char buffer[BUFFER_SIZE] = { 0 };
	char *buffer_end = buffer;
	size_t buffer_pos = 0;
	size_t to_copy_size = 0;
	size_t next_pos = 0;

	// create the packet that will be copied
	Packet *packet = packet_generate_request (
		PACKET_TYPE_APP, APP_REQUEST_MESSAGE,
		MESSAGE, strlen (MESSAGE)
	);

	char *packet_offset = packet->packet;

	size_t DATA_LEN = packet->packet_size;

	int64_t iterations = kBytes / (int64_t) DATA_LEN;

	ReceiveHandle *receive_handle = receive_handle_new ();
	receive_handle->buffer = buffer;
	receive_handle->buffer_size = BUFFER_SIZE;
	receive_handle->received_size = BUFFER_SIZE;
	receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;

	struct timeval start = { 0 };
	struct timeval end = { 0 };

	(void) gettimeofday (&start, NULL);

	// bench
	register int64_t i = 0;
	for (i = 0; i < iterations; i++) {
		do {
			next_pos = buffer_pos + DATA_LEN;

			// copy the data to the buffer
			if (next_pos <= BUFFER_SIZE) {
				(void) memcpy (buffer_end, packet_offset, DATA_LEN);
				buffer_pos += DATA_LEN;
				buffer_end += DATA_LEN;

				packet_offset = packet->packet;
			}

			else {
				// copy what we can to the buffer
				to_copy_size = BUFFER_SIZE - buffer_pos;
				(void) memcpy (buffer_end, packet->packet, to_copy_size);
				packet_offset += to_copy_size;
			}
		} while (buffer_pos >= BUFFER_SIZE);
		
		// parse the packet
		cerver_receive_handle_buffer (receive_handle);

		buffer_pos = 0;
		buffer_end = buffer;
	}

	// results
	double elapsed = 0.0;
	double bw = 0.0;
	double total = 0.0;

	(void) gettimeofday (&end, NULL);

	(void) fprintf (stdout, "Benchmark result:\n");

	elapsed = (double) (end.tv_sec - start.tv_sec) +
		(end.tv_usec - start.tv_usec) * 1e-6f;

	total = (double) iterations * DATA_LEN;
	bw = (double) total / elapsed;

	(void) fprintf (
		stdout,
		"%.2f mb | %.2f mb/s | %.2f req/sec | %.2f s\n",
		(double) total / (1024 * 1024),
		bw / (1024 * 1024),
		(double) iterations / elapsed,
		elapsed
	);

	// clean
	packet_delete (packet);
	receive_handle_delete (receive_handle);

	return 0;

}