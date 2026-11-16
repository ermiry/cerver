#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <signal.h>
#include <unistd.h>

#include <sys/socket.h>

#include <cerver/types/types.h>

#include "cerver/events/internal.h"

#include "../test.h"
#include "events.h"

// the sources that a wait of the tests reports at most
#define POLLER_TEST_READY					8

// a wait that must not find anything returns after these milliseconds
#define POLLER_TEST_SHORT_WAIT				16

// a wait that must be woken up fails the test if it ends by this timeout
#define POLLER_TEST_LONG_WAIT				8000

// the wake ups that are sent before a single wait
#define POLLER_TEST_WAKES					64

// the value that a source has before it is set
#define POLLER_TEST_FILL					0x5A

static const char poller_test_byte = 'x';

typedef struct PollerTestPair {

	int fds[2];

} PollerTestPair;

static void poller_test_pair_open (PollerTestPair *pair) {

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, pair->fds), 0, NULL);

}

static void poller_test_pair_close (PollerTestPair *pair) {

	if (pair->fds[0] > -1) (void) close (pair->fds[0]);
	if (pair->fds[1] > -1) (void) close (pair->fds[1]);

}

static void poller_test_write (const int fd) {

	test_check_long_int_eq ((long) write (fd, &poller_test_byte, sizeof (poller_test_byte)), 1, NULL);

}

static void test_poller_source_init (void) {

	EventSource source;
	(void) memset (&source, POLLER_TEST_FILL, sizeof (EventSource));

	int owner = 0;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, &owner);

	test_check_int_eq ((int) source.type, (int) EVENT_SOURCE_TYPE_CONNECTION, NULL);
	test_check_int_eq (source.fd, -1, NULL);
	test_check_unsigned_eq (source.interest, EVENT_INTEREST_NONE, NULL);
	test_check_false (source.registered);
	test_check_ptr_eq (source.owner, &owner);

	// a NULL source is ignored
	event_source_init (NULL, EVENT_SOURCE_TYPE_LISTEN, NULL);

}

static void test_poller_create (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	// nothing is ready in a poller without sources
	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, 0), 0, NULL);

	event_poller_delete (poller);

	event_poller_delete (NULL);

}

static void test_poller_null (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, NULL);

	EventReady ready[POLLER_TEST_READY];

	test_check_unsigned_eq (event_poller_add (NULL, &source, 0, EVENT_INTEREST_READ), 1, NULL);
	test_check_unsigned_eq (event_poller_add (poller, NULL, 0, EVENT_INTEREST_READ), 1, NULL);
	test_check_unsigned_eq (event_poller_add (poller, &source, -1, EVENT_INTEREST_READ), 1, NULL);
	test_check_false (source.registered);

	// a source that is not registered can not be modified, removing it is not an error
	test_check_unsigned_eq (event_poller_modify (poller, &source, EVENT_INTEREST_WRITE), 1, NULL);
	test_check_unsigned_eq (event_poller_remove (poller, &source), 0, NULL);
	test_check_unsigned_eq (event_poller_remove (NULL, &source), 1, NULL);
	test_check_unsigned_eq (event_poller_remove (poller, NULL), 1, NULL);

	test_check_int_eq (event_poller_wait (NULL, ready, POLLER_TEST_READY, 0), -1, NULL);
	test_check_int_eq (event_poller_wait (poller, NULL, POLLER_TEST_READY, 0), -1, NULL);
	test_check_int_eq (event_poller_wait (poller, ready, 0, 0), -1, NULL);

	event_poller_wake (NULL);

	event_poller_delete (poller);

}

static void test_poller_read (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	PollerTestPair pair;
	poller_test_pair_open (&pair);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, &pair);

	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_READ), 0, NULL);
	test_check_true (source.registered);
	test_check_int_eq (source.fd, pair.fds[0], NULL);

	// a source can not be registered twice
	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_READ), 1, NULL);

	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_SHORT_WAIT), 0, NULL);

	poller_test_write (pair.fds[1]);

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check_ptr_eq (ready[0].source, &source);
	test_check_ptr_eq (ready[0].source->owner, &pair);
	test_check_unsigned_eq (ready[0].ready, EVENT_READY_READ, NULL);

	// level triggered: the data that was not read is reported again
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, 0), 1, NULL);

	char byte = 0;
	test_check_long_int_eq ((long) read (pair.fds[0], &byte, sizeof (byte)), 1, NULL);

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, 0), 0, NULL);

	test_check_unsigned_eq (event_poller_remove (poller, &source), 0, NULL);
	test_check_false (source.registered);
	test_check_int_eq (source.fd, -1, NULL);

	poller_test_pair_close (&pair);

	event_poller_delete (poller);

}

static void test_poller_write (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	PollerTestPair pair;
	poller_test_pair_open (&pair);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, NULL);

	// an empty socket can always be written
	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_WRITE), 0, NULL);

	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check_unsigned_eq (ready[0].ready, EVENT_READY_WRITE, NULL);

	// without the interest it is not reported anymore
	test_check_unsigned_eq (event_poller_modify (poller, &source, EVENT_INTEREST_READ), 0, NULL);
	test_check_unsigned_eq (source.interest, EVENT_INTEREST_READ, NULL);
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_SHORT_WAIT), 0, NULL);

	// the same interest is not an error
	test_check_unsigned_eq (event_poller_modify (poller, &source, EVENT_INTEREST_READ), 0, NULL);

	// both at once
	poller_test_write (pair.fds[1]);
	test_check_unsigned_eq (
		event_poller_modify (poller, &source, EVENT_INTEREST_READ | EVENT_INTEREST_WRITE), 0, NULL
	);

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check_unsigned_eq (ready[0].ready, EVENT_READY_READ | EVENT_READY_WRITE, NULL);

	(void) event_poller_remove (poller, &source);

	poller_test_pair_close (&pair);

	event_poller_delete (poller);

}

// a peer that closes is a read: the owner finds the end with its recv ()
static void test_poller_peer_closed (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	PollerTestPair pair;
	poller_test_pair_open (&pair);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, NULL);

	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_READ), 0, NULL);

	(void) close (pair.fds[1]);
	pair.fds[1] = -1;

	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check ((ready[0].ready & EVENT_READY_READ) != 0, "a closed peer was not reported as a read");

	char byte = 0;
	test_check_long_int_eq ((long) read (pair.fds[0], &byte, sizeof (byte)), 0, NULL);

	(void) event_poller_remove (poller, &source);

	poller_test_pair_close (&pair);

	event_poller_delete (poller);

}

// a hang up is reported with any interest, also without one: why a fd that
// another thread owns is removed and not only left without interest
static void test_poller_error_without_interest (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	PollerTestPair pair;
	poller_test_pair_open (&pair);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, NULL);

	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_NONE), 0, NULL);

	EventReady ready[POLLER_TEST_READY];

	// data is not reported without the interest
	poller_test_write (pair.fds[1]);
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_SHORT_WAIT), 0, NULL);

	(void) close (pair.fds[1]);
	pair.fds[1] = -1;

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check ((ready[0].ready & EVENT_READY_ERROR) != 0, "the hang up was not reported");

	// once it is removed nothing is reported
	test_check_unsigned_eq (event_poller_remove (poller, &source), 0, NULL);
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_SHORT_WAIT), 0, NULL);

	poller_test_pair_close (&pair);

	event_poller_delete (poller);

}

static void test_poller_many_sources (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	PollerTestPair pairs[POLLER_TEST_READY];
	EventSource sources[POLLER_TEST_READY];

	for (int idx = 0; idx < POLLER_TEST_READY; idx++) {
		poller_test_pair_open (&pairs[idx]);
		event_source_init (&sources[idx], EVENT_SOURCE_TYPE_CONNECTION, &pairs[idx]);

		test_check_unsigned_eq (
			event_poller_add (poller, &sources[idx], pairs[idx].fds[0], EVENT_INTEREST_READ), 0, NULL
		);

		poller_test_write (pairs[idx].fds[1]);
	}

	// a wait never reports more than what fits
	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (event_poller_wait (poller, ready, 1, POLLER_TEST_LONG_WAIT), 1, NULL);

	test_check_int_eq (
		event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), POLLER_TEST_READY, NULL
	);

	// each source once, with its owner
	bool found[POLLER_TEST_READY] = { false };
	for (int idx = 0; idx < POLLER_TEST_READY; idx++) {
		const long position = (PollerTestPair *) ready[idx].source->owner - pairs;
		test_check ((position >= 0) && (position < POLLER_TEST_READY), "a source that was not registered");
		test_check_false (found[position]);
		found[position] = true;
	}

	for (int idx = 0; idx < POLLER_TEST_READY; idx++) {
		(void) event_poller_remove (poller, &sources[idx]);
		poller_test_pair_close (&pairs[idx]);
	}

	event_poller_delete (poller);

}

// a wake up ends the wait without a source, and many of them are one
static void test_poller_wake (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	EventReady ready[POLLER_TEST_READY];

	for (int idx = 0; idx < POLLER_TEST_WAKES; idx++) event_poller_wake (poller);

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 0, NULL);

	// they were consumed by that wait
	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_SHORT_WAIT), 0, NULL);

	// a wake up does not hide a source that is ready
	PollerTestPair pair;
	poller_test_pair_open (&pair);

	EventSource source;
	event_source_init (&source, EVENT_SOURCE_TYPE_CONNECTION, NULL);
	test_check_unsigned_eq (event_poller_add (poller, &source, pair.fds[0], EVENT_INTEREST_READ), 0, NULL);

	poller_test_write (pair.fds[1]);
	event_poller_wake (poller);

	test_check_int_eq (event_poller_wait (poller, ready, POLLER_TEST_READY, POLLER_TEST_LONG_WAIT), 1, NULL);
	test_check_ptr_eq (ready[0].source, &source);

	(void) event_poller_remove (poller, &source);

	poller_test_pair_close (&pair);

	event_poller_delete (poller);

}

static void *poller_test_wake_thread (void *poller_ptr) {

	event_poller_wake ((const EventPoller *) poller_ptr);

	return NULL;

}

// the wake up of another thread ends a wait without a timeout
static void test_poller_wake_from_thread (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, poller_test_wake_thread, poller), 0, NULL);

	EventReady ready[POLLER_TEST_READY];
	test_check_int_eq (
		event_poller_wait (poller, ready, POLLER_TEST_READY, EVENT_POLLER_WAIT_FOREVER), 0, NULL
	);

	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	event_poller_delete (poller);

}

// the errno of the caller is kept: the wake up is used by a signal handler
static void test_poller_wake_keeps_errno (void) {

	EventPoller *poller = event_poller_create (0);
	test_check_ptr (poller);

	errno = ENOENT;
	event_poller_wake (poller);
	test_check_int_eq (errno, ENOENT, NULL);

	event_poller_delete (poller);

}

void events_tests_poller (void) {

	(void) printf ("\nTesting POLLER...\n");

	test_run (test_poller_source_init);
	test_run (test_poller_create);
	test_run (test_poller_null);
	test_run (test_poller_read);
	test_run (test_poller_write);
	test_run (test_poller_peer_closed);
	test_run (test_poller_error_without_interest);
	test_run (test_poller_many_sources);
	test_run (test_poller_wake);
	test_run (test_poller_wake_from_thread);
	test_run (test_poller_wake_keeps_errno);

	(void) printf ("Done!\n");

}
