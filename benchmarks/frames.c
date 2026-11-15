#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/message.h>
#include <cerver/http/websockets/text.h>

// the WebSocket receiver of cerver alone: the masked frames of a client (small and large)
// parsed and unmasked, the messages assembled and their text validated,
// and the mask and the UTF-8 validation by themselves
// usage: frames [megabytes]

#define FRAMES_DEFAULT_MEGABYTES		1024
#define FRAMES_ARGUMENT_MEGABYTES		1

#define FRAMES_BYTES_PER_MEGABYTE		(1024.0 * 1024.0)
#define FRAMES_NANOSECONDS_PER_SECOND	1e9

// the frames of each round, about 1 MiB
#define FRAMES_ROUND_SIZE				(1 << 20)

#define FRAMES_SMALL_PAYLOAD			64
#define FRAMES_LARGE_PAYLOAD			(1 << 16)

static const u8 frames_mask[HTTP_WEB_SOCKET_MASK_SIZE] = { 0x37, 0xFA, 0x21, 0x3D };

static u64 frames_messages = 0;

// the payload that every message of a round has, and the messages that were not it
static const u8 *frames_expected = NULL;
static size_t frames_expected_size = 0;
static u64 frames_wrong = 0;

static double frames_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / FRAMES_NANOSECONDS_PER_SECOND);

}

static int frames_on_message (void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size) {

	(void) data;
	(void) type;

	frames_messages += 1;

	// the receiver unmasked the payload that was sent
	if ((size != frames_expected_size) || memcmp (payload, frames_expected, size)) frames_wrong += 1;

	return 0;

}

static const HttpWebSocketReceiverHandlers frames_handlers = {
	frames_on_message, NULL, NULL, NULL, NULL
};

// a round of masked text frames of the payload, that is the one that every message must have
// returns the bytes of the round, and sets its frames
static size_t frames_round (u8 *round, const u8 *payload, const size_t payload_size, u64 *frames) {

	size_t size = 0;
	while ((size + payload_size + HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE) <= FRAMES_ROUND_SIZE) {
		const size_t header = http_web_socket_frame_header (
			round + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, payload_size, frames_mask
		);

		(void) memcpy (round + size + header, payload, payload_size);
		http_web_socket_mask (round + size + header, payload_size, frames_mask, 0);
		size += header + payload_size;

		*frames += 1;
	}

	return size;

}

// the receiver unmasks in place, so each round is parsed from a fresh copy
// returns 0 when the receiver gave every message of every round as it was sent, 1 if it did not:
// its result and its messages were ignored, a receiver that failed in its first frame was the fastest one
static int frames_receiver (const char *name, const size_t payload_size, const size_t megabytes) {

	u8 *round = (u8 *) malloc (FRAMES_ROUND_SIZE);
	u8 *copy = (u8 *) malloc (FRAMES_ROUND_SIZE);

	u8 *payload = (u8 *) malloc (payload_size);
	for (size_t i = 0; i < payload_size; i++) payload[i] = (u8) ('a' + (i % 26));

	u64 round_frames = 0;
	const size_t round_size = frames_round (round, payload, payload_size, &round_frames);

	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, HTTP_WEB_SOCKET_ROLE_SERVER, payload_size, payload_size, NULL, &frames_handlers, NULL
	);

	frames_expected = payload;
	frames_expected_size = payload_size;
	frames_messages = 0;
	frames_wrong = 0;

	size_t parsed = 0;
	u64 rounds = 0;
	u64 stopped = 0;
	double elapsed = 0;
	const size_t total = megabytes * (size_t) FRAMES_BYTES_PER_MEGABYTE;
	while (parsed < total) {
		(void) memcpy (copy, round, round_size);

		const double start = frames_now ();
		const HttpWebSocketReceiveResult result = http_web_socket_receiver_execute (&receiver, copy, round_size, NULL);
		elapsed += frames_now () - start;

		if (result != HTTP_WEB_SOCKET_RECEIVE_CONTINUE) stopped += 1;

		parsed += round_size;
		rounds += 1;
	}

	(void) printf (
		"%s: %.2f MB/s - %.0f messages/s\n",
		name, ((double) parsed / FRAMES_BYTES_PER_MEGABYTE) / elapsed, (double) frames_messages / elapsed
	);

	const bool whole = !stopped && !frames_wrong && (frames_messages == (rounds * round_frames));
	if (!whole) {
		(void) fprintf (
			stderr, "%s: %llu messages of %llu, %llu wrong, %llu rounds stopped\n", name,
			(unsigned long long) frames_messages, (unsigned long long) (rounds * round_frames),
			(unsigned long long) frames_wrong, (unsigned long long) stopped
		);
	}

	http_web_socket_receiver_end (&receiver);
	free (round);
	free (copy);
	free (payload);

	return whole ? 0 : 1;

}

static void frames_mask_alone (const size_t megabytes) {

	u8 *data = (u8 *) calloc (1, FRAMES_ROUND_SIZE);

	const size_t rounds = megabytes;
	const double start = frames_now ();
	for (size_t i = 0; i < rounds; i++) http_web_socket_mask (data, FRAMES_ROUND_SIZE, frames_mask, i);
	const double elapsed = frames_now () - start;

	(void) printf ("mask: %.2f MB/s\n", ((double) rounds * FRAMES_ROUND_SIZE / FRAMES_BYTES_PER_MEGABYTE) / elapsed);

	free (data);

}

static void frames_text_alone (const size_t megabytes) {

	u8 *data = (u8 *) malloc (FRAMES_ROUND_SIZE);

	// ASCII with a code point of two, three and four bytes every 64 bytes
	static const u8 code_points[] = { 0xC3, 0xA9, 0xE2, 0x82, 0xAC, 0xF0, 0x9F, 0x98, 0x80 };
	for (size_t i = 0; i < FRAMES_ROUND_SIZE; i++) data[i] = (u8) ('a' + (i % 26));
	for (size_t i = 0; (i + sizeof (code_points)) < FRAMES_ROUND_SIZE; i += FRAMES_SMALL_PAYLOAD) {
		(void) memcpy (data + i, code_points, sizeof (code_points));
	}

	bool valid = true;
	const double start = frames_now ();
	for (size_t i = 0; i < megabytes; i++) valid = valid && http_web_socket_text_check (data, FRAMES_ROUND_SIZE);
	const double elapsed = frames_now () - start;

	(void) printf (
		"utf-8 (%s): %.2f MB/s\n", valid ? "valid" : "invalid",
		((double) megabytes * FRAMES_ROUND_SIZE / FRAMES_BYTES_PER_MEGABYTE) / elapsed
	);

	free (data);

}

int main (int argc, char **argv) {

	const size_t megabytes = (argc > FRAMES_ARGUMENT_MEGABYTES) ?
		(size_t) atoi (argv[FRAMES_ARGUMENT_MEGABYTES]) : FRAMES_DEFAULT_MEGABYTES;

	int failed = frames_receiver ("small frames (64 bytes)", FRAMES_SMALL_PAYLOAD, megabytes);
	failed |= frames_receiver ("large frames (64 KiB)", FRAMES_LARGE_PAYLOAD, megabytes);
	frames_mask_alone (megabytes);
	frames_text_alone (megabytes);

	return failed;

}
