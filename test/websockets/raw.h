#ifndef _CERVER_TESTS_WEBSOCKET_RAW_H_
#define _CERVER_TESTS_WEBSOCKET_RAW_H_

#include <stdbool.h>
#include <stddef.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/frame.h>

// the most frames that a reader keeps until they are taken
#define RAW_MAX_FRAMES					64

#define RAW_BUFFER_SIZE					(1 << 16)

// milliseconds that a reader waits for a frame by default
#define RAW_TIMEOUT						(1 << 13)

// the most bytes of the response of a handshake
#define RAW_RESPONSE_SIZE				(1 << 12)

// the request of an upgrade to the path with extra headers (that end with \r\n)
#define RAW_UPGRADE_FORMAT										\
	"GET %s HTTP/1.1\r\n"										\
	"Host: localhost\r\n"										\
	"Upgrade: websocket\r\n"									\
	"Connection: Upgrade\r\n"									\
	"Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"			\
	"Sec-WebSocket-Version: 13\r\n"								\
	"%s"														\
	"\r\n"

typedef struct RawFrame {

	bool fin;
	bool rsv1;
	u8 opcode;

	size_t size;
	u8 *data;

} RawFrame;

// reads the frames of a server, a frame can arrive in many reads
typedef struct RawReader {

	int fd;

	HttpWebSocketParser parser;

	RawFrame current;

	unsigned int n_frames;
	RawFrame frames[RAW_MAX_FRAMES];

	// the bytes that were received while the frames were full, parsed before the next recv ()
	u8 pending[RAW_BUFFER_SIZE];
	size_t pending_size;

	// the connection ended (EOF or an error)
	bool closed;

} RawReader;

// connects to the cerver in the family of CERVER_TEST_ADDRESS, retrying until it listens
extern int raw_connect (const u16 port);

// sends every byte
extern void raw_send (const int fd, const void *data, const size_t size);

// sends an upgrade and reads its response until its empty line,
// the bytes after it are left in the socket for the reader
// returns the status of the response, 0 if it did not arrive
extern int raw_upgrade (const int fd, const char *path, const char *headers, char *response);

// connects and upgrades, the connection must be accepted
extern int raw_open (const u16 port, const char *path, const char *headers);

// writes a frame with the mask of a client (or without it when masked is false)
// returns its size
extern size_t raw_frame (
	u8 *buffer, const bool fin, const bool rsv1, const u8 opcode,
	const void *payload, const size_t size, const bool masked
);

// sends a frame of a client
extern void raw_send_frame (
	const int fd, const bool fin, const u8 opcode,
	const void *payload, const size_t size
);

// sends a close frame with its code and its reason
extern void raw_send_close (const int fd, const u16 code, const char *reason);

extern void raw_reader_init (RawReader *reader, const int fd);

extern void raw_reader_end (RawReader *reader);

// takes the next frame of the server, waiting up to timeout milliseconds
// returns false on a timeout or when the connection ended without it
extern bool raw_read_frame (RawReader *reader, RawFrame *frame, const int timeout);

extern void raw_frame_delete (RawFrame *frame);

// the code of a close frame, 1005 when it has none
extern u16 raw_close_code (const RawFrame *frame);

// waits until the server ends the TCP connection, the frames before it are discarded
// returns true when it ended before the timeout
extern bool raw_wait_end (RawReader *reader, const int timeout);

// ends the connection with a RST
extern void raw_reset (const int fd);

// the fds of the process, to check that nothing was left open
extern unsigned int raw_open_fds (void);

// prints the fds of the process and what each one is
extern void raw_print_fds (void);

#endif
