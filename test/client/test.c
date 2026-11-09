#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <pthread.h>

#include <cerver/client.h>

#include "../test.h"

static const char *client_name = "test-client";

static Client *test_client_create (void) {

	Client *client = client_create ();

	test_check_str_eq (client->name, CLIENT_DEFAULT_NAME, NULL);
	test_check_str_len (client->name, strlen (CLIENT_DEFAULT_NAME), NULL);
	test_check_unsigned_ne (client->connected_timestamp, 0);
	test_check_ptr (client->connections);
	test_check_ptr (client->lock);
	test_check_ptr (client->file_stats);
	test_check_ptr (client->stats);

	return client;

}

static void test_client_base_configuration (void) {

	Client *client = test_client_create ();

	client_set_name (client,client_name);
	test_check_str_eq (client->name, client_name, NULL);
	test_check_str_len (client->name, strlen (client_name), NULL);

	client_delete (client);

}

#define CLIENT_SESSION_ID					"session-0123456789abcdef"
#define CLIENT_SESSION_OTHER				"session-other"
#define CLIENT_SESSION_SHORT_SIZE			8
#define CLIENT_SESSION_BUFFER_SIZE			64
#define CLIENT_SESSION_THREADS				4
#define CLIENT_SESSION_ROUNDS				(1 << 12)

// the session id is copied with its length and cut to the buffer size
static void test_client_session_id (void) {

	Client *client = test_client_create ();

	char session_id[CLIENT_SESSION_BUFFER_SIZE] = { 0 };
	test_check_unsigned_eq (client_get_session_id (client, session_id, sizeof (session_id)), 0, NULL);
	test_check_str_empty (session_id);

	test_check_unsigned_eq (client_set_session_id (client, CLIENT_SESSION_ID), 0, NULL);
	test_check_unsigned_eq (
		client_get_session_id (client, session_id, sizeof (session_id)),
		strlen (CLIENT_SESSION_ID), NULL
	);
	test_check_str_eq (session_id, CLIENT_SESSION_ID, NULL);

	char short_id[CLIENT_SESSION_SHORT_SIZE] = { 0 };
	test_check_unsigned_eq (
		client_get_session_id (client, short_id, sizeof (short_id)),
		CLIENT_SESSION_SHORT_SIZE - 1, NULL
	);
	test_check_str_len (short_id, CLIENT_SESSION_SHORT_SIZE - 1, NULL);

	test_check_unsigned_eq (client_set_session_id (client, NULL), 0, NULL);
	test_check_unsigned_eq (client_get_session_id (client, session_id, sizeof (session_id)), 0, NULL);

	client_delete (client);

}

static void *client_session_writer (void *client_ptr) {

	Client *client = (Client *) client_ptr;

	for (unsigned int i = 0; i < CLIENT_SESSION_ROUNDS; i++) {
		(void) client_set_session_id (client, (i % 2) ? CLIENT_SESSION_ID : CLIENT_SESSION_OTHER);
	}

	return NULL;

}

static void *client_session_reader (void *client_ptr) {

	const Client *client = (const Client *) client_ptr;

	char session_id[CLIENT_SESSION_BUFFER_SIZE] = { 0 };
	for (unsigned int i = 0; i < CLIENT_SESSION_ROUNDS; i++) {
		(void) client_get_session_id (client, session_id, sizeof (session_id));

		// a reader never sees a half written id (TSan checks the race)
		test_check (
			!strcmp (session_id, CLIENT_SESSION_ID) || !strcmp (session_id, CLIENT_SESSION_OTHER),
			"a reader saw an invalid session id"
		);
	}

	return NULL;

}

// the session id is set and read from many threads at the same time
static void test_client_session_id_threads (void) {

	Client *client = test_client_create ();
	test_check_unsigned_eq (client_set_session_id (client, CLIENT_SESSION_ID), 0, NULL);

	pthread_t threads[CLIENT_SESSION_THREADS];
	for (unsigned int i = 0; i < CLIENT_SESSION_THREADS; i++) {
		test_check_int_eq (
			pthread_create (&threads[i], NULL, (i % 2) ? client_session_reader : client_session_writer, client),
			0, NULL
		);
	}

	for (unsigned int i = 0; i < CLIENT_SESSION_THREADS; i++) {
		(void) pthread_join (threads[i], NULL);
	}

	client_delete (client);

}

#define CLIENT_RECYCLE_ROUNDS				(1 << 4)
#define CLIENT_RECYCLE_BYTES				128

// a deleted client is reused: every value of the previous one is reset,
// and its stats, lock and connections list are its own
static void test_client_recycled (void) {

	for (unsigned int i = 0; i < CLIENT_RECYCLE_ROUNDS; i++) {
		Client *client = test_client_create ();

		test_check_unsigned_eq (dlist_size (client->connections), 0, NULL);
		test_check_unsigned_eq (client->stats->total_bytes_received, 0, NULL);
		test_check_unsigned_eq (client->stats->received_packets->n_app_packets, 0, NULL);
		test_check_unsigned_eq (client->file_stats->n_files_requests, 0, NULL);
		test_check_null_ptr (client->session_id);
		test_check_ptr_eq (client->lock, &client->lock_storage);
		test_check_ptr_eq (client->stats->received_packets, &client->stats->received_packets_storage);

		// values that the next client must not see
		client->stats->total_bytes_received = CLIENT_RECYCLE_BYTES;
		client->stats->received_packets->n_app_packets = CLIENT_RECYCLE_BYTES;
		client->file_stats->n_files_requests = CLIENT_RECYCLE_BYTES;
		test_check_unsigned_eq (client_set_session_id (client, CLIENT_SESSION_ID), 0, NULL);
		client_set_name (client, client_name);

		client_delete (client);
	}

}

int main (int argc, char **argv) {

	test_output_init ();

	srand ((unsigned) time (NULL));

	(void) printf ("Testing CLIENT...\n");

	test_client_base_configuration ();
	test_client_session_id ();
	test_client_session_id_threads ();
	test_client_recycled ();

	(void) printf ("\nDone with CLIENT tests!\n\n");

	return 0;

}