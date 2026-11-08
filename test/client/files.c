#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>

#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/events.h>
#include <cerver/files.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include "../test.h"

// the file transfers between a files cerver and a client in the same process:
// - the client gets a file that the cerver serves, with the same bytes
// - the client sends a file that the cerver saves in its uploads path, with the same bytes
// - a file that does not exist is answered with a CLIENT_ERROR_FILE_NOT_FOUND
// - a get without the client's uploads path is not sent

#define FILES_ADDRESS					"127.0.0.1"
#define FILES_PORT						7500
#define FILES_NAME						"files-cerver"

#define FILES_THREADS					4
#define FILES_POLL_TIMEOUT				200
#define FILES_JOIN_SECONDS				8

#define FILES_DIRECTORY_MODE			0777

#define FILES_ROOT						"/tmp/cerver-test-files"
#define FILES_SERVED_PATH				FILES_ROOT "/served"
#define FILES_CERVER_UPLOADS_PATH		FILES_ROOT "/cerver-uploads"
#define FILES_CLIENT_UPLOADS_PATH		FILES_ROOT "/client-uploads"
#define FILES_SEND_PATH					FILES_ROOT "/send"

#define FILES_SERVED_NAME				"served.bin"
#define FILES_SENT_NAME					"sent.bin"
#define FILES_MISSING_NAME				"missing.bin"

#define FILES_SIZE						(1 << 18)
#define FILES_BYTE_PATTERN				0xFF
#define FILES_SENT_SEED					7

#define FILES_FILENAME_SIZE				512

static unsigned int cerver_started = 0;

static unsigned int cerver_uploads = 0;
static unsigned int client_uploads = 0;
static unsigned int not_found_errors = 0;

static char cerver_saved_filename[FILES_FILENAME_SIZE] = { 0 };
static char client_saved_filename[FILES_FILENAME_SIZE] = { 0 };

static pthread_t cerver_thread = 0;
static u8 cerver_start_result = 0;

#pragma region files

static char files_byte (const size_t idx, const unsigned int seed) {

	return (char) ((idx + seed) & FILES_BYTE_PATTERN);

}

static void files_create (const char *filename, const size_t size, const unsigned int seed) {

	FILE *file = fopen (filename, "wb");
	test_check_ptr (file);

	for (size_t i = 0; i < size; i++) (void) fputc (files_byte (i, seed), file);

	(void) fclose (file);

}

static bool files_check (const char *filename, const size_t size, const unsigned int seed) {

	bool valid = false;

	FILE *file = fopen (filename, "rb");
	if (file) {
		valid = true;

		size_t i = 0;
		int c = 0;
		while ((c = fgetc (file)) != EOF) {
			if ((i >= size) || ((char) c != files_byte (i, seed))) {
				valid = false;
				break;
			}

			i += 1;
		}

		if (i != size) valid = false;

		(void) fclose (file);
	}

	return valid;

}

static void files_directories_create (void) {

	(void) mkdir (FILES_ROOT, FILES_DIRECTORY_MODE);
	(void) mkdir (FILES_SERVED_PATH, FILES_DIRECTORY_MODE);
	(void) mkdir (FILES_CERVER_UPLOADS_PATH, FILES_DIRECTORY_MODE);
	(void) mkdir (FILES_CLIENT_UPLOADS_PATH, FILES_DIRECTORY_MODE);
	(void) mkdir (FILES_SEND_PATH, FILES_DIRECTORY_MODE);

	files_create (FILES_SERVED_PATH "/" FILES_SERVED_NAME, FILES_SIZE, 0);
	files_create (FILES_SEND_PATH "/" FILES_SENT_NAME, FILES_SIZE, FILES_SENT_SEED);

}

#pragma endregion

#pragma region cerver

static void files_cerver_upload_cb (
	Cerver *cerver, Client *client, Connection *connection,
	const char *saved_filename
) {

	(void) cerver;
	(void) client;
	(void) connection;

	(void) snprintf (cerver_saved_filename, FILES_FILENAME_SIZE, "%s", saved_filename);

	test_counter_add (&cerver_uploads);

}

static void *files_cerver_started (void *event_data_ptr) {

	cerver_event_data_delete ((CerverEventData *) event_data_ptr);

	test_counter_add (&cerver_started);

	return NULL;

}

static void *files_cerver_thread (void *cerver_ptr) {

	cerver_start_result = cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static Cerver *files_cerver_start (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_FILES, FILES_NAME,
		FILES_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, FILES_THREADS);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (cerver, FILES_POLL_TIMEOUT);

	FileCerver *file_cerver = (FileCerver *) cerver->cerver_data;
	test_check_ptr (file_cerver);

	test_check_unsigned_eq (file_cerver_add_path (file_cerver, FILES_SERVED_PATH), 0, NULL);
	file_cerver_set_uploads_path (file_cerver, FILES_CERVER_UPLOADS_PATH);
	file_cerver_set_file_upload_cb (file_cerver, files_cerver_upload_cb);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_STARTED,
		files_cerver_started, NULL, NULL,
		false, false
	);

	test_check_int_eq (pthread_create (&cerver_thread, NULL, files_cerver_thread, cerver), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_started, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

	return cerver;

}

static void files_cerver_stop (Cerver *cerver) {

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += FILES_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (cerver_thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (cerver_start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region client

static void files_client_upload_cb (
	Client *client, Connection *connection,
	const char *saved_filename
) {

	(void) client;
	(void) connection;

	(void) snprintf (client_saved_filename, FILES_FILENAME_SIZE, "%s", saved_filename);

	test_counter_add (&client_uploads);

}

static void *files_client_not_found (void *error_data_ptr) {

	client_error_data_delete ((ClientErrorData *) error_data_ptr);

	test_counter_add (&not_found_errors);

	return NULL;

}

static Client *files_client_create (Connection **connection) {

	Client *client = client_create ();
	test_check_ptr (client);

	client_files_set_file_upload_cb (client, files_client_upload_cb);

	(void) client_error_register (
		client, CLIENT_ERROR_FILE_NOT_FOUND,
		files_client_not_found, NULL, NULL,
		false, false
	);

	*connection = client_connection_create (
		client, FILES_ADDRESS, FILES_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (*connection);

	test_check_int_eq (client_connect_and_start (client, *connection), 0, NULL);

	return client;

}

#pragma endregion

#pragma region tests

static void test_files_get_without_uploads_path (Client *client, Connection *connection) {

	test_check_unsigned_ne (client_file_get (client, connection, FILES_SERVED_NAME), 0);

}

static void test_files_get (Client *client, Connection *connection) {

	client_files_set_uploads_path (client, FILES_CLIENT_UPLOADS_PATH);

	test_check_unsigned_eq (client_file_get (client, connection, FILES_SERVED_NAME), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&client_uploads, 1, TEST_WAIT_SECONDS),
		1, "the file did not arrive"
	);

	test_check (files_check (client_saved_filename, FILES_SIZE, 0), "the received file is different");

}

static void test_files_get_missing (Client *client, Connection *connection) {

	test_check_unsigned_eq (client_file_get (client, connection, FILES_MISSING_NAME), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&not_found_errors, 1, TEST_WAIT_SECONDS),
		1, "the missing file was not reported"
	);

}

static void test_files_send (Client *client, Connection *connection) {

	test_check_unsigned_eq (
		client_file_send (client, connection, FILES_SEND_PATH "/" FILES_SENT_NAME), 0, NULL
	);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_uploads, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not save the file"
	);

	test_check (files_check (cerver_saved_filename, FILES_SIZE, FILES_SENT_SEED), "the saved file is different");

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing CLIENT files...\n");

	files_directories_create ();

	Cerver *cerver = files_cerver_start ();

	Connection *connection = NULL;
	Client *client = files_client_create (&connection);

	test_files_get_without_uploads_path (client, connection);
	test_files_get (client, connection);
	test_files_get_missing (client, connection);
	test_files_send (client, connection);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	files_cerver_stop (cerver);

	(void) printf ("Done with CLIENT files tests!\n\n");

	return 0;

}
