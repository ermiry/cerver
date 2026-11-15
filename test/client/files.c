#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
#include <cerver/client/requests.h>
#include <cerver/connection/connection.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/files/cerver.h>
#include <cerver/files/files.h>
#include <cerver/handler/events.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include "../test.h"

// the file transfers between a files cerver and a client in the same process:
// - the client gets a file that the cerver serves, with the same bytes
// - the client sends a file that the cerver saves in its uploads path, with the same bytes
// - a file that does not exist is answered with a CLIENT_ERROR_FILE_NOT_FOUND
// - a get without the client's uploads path is not sent
// - an upload to a cerver without an uploads path is answered with a CLIENT_ERROR_SEND_FILE,
// its connection is ended, and the cerver keeps running

#define FILES_ADDRESS					"127.0.0.1"
#define FILES_PORT						7500
#define FILES_NAME						"files-cerver"

#define FILES_WITHOUT_UPLOADS_PORT		7501
#define FILES_WITHOUT_UPLOADS_NAME		"files-cerver-without-uploads"

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

// it exists outside of the served path, relative to it
#define FILES_OUTSIDE_NAME				"../send/" FILES_SENT_NAME
#define FILES_OUTSIDE_WAIT_SECONDS		1

#define FILES_SIZE						(1 << 18)
#define FILES_BYTE_PATTERN				0xFF
#define FILES_SENT_SEED					7

#define FILES_FILENAME_SIZE				512

typedef struct FilesCerver {

	Cerver *cerver;
	pthread_t thread;
	u8 start_result;

} FilesCerver;

static unsigned int cerver_started = 0;

static unsigned int cerver_uploads = 0;
static unsigned int client_uploads = 0;
static unsigned int not_found_errors = 0;
static unsigned int send_file_errors = 0;

static char cerver_saved_filename[FILES_FILENAME_SIZE] = { 0 };
static char client_saved_filename[FILES_FILENAME_SIZE] = { 0 };

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

static void *files_cerver_thread (void *files_cerver_ptr) {

	FilesCerver *files_cerver = (FilesCerver *) files_cerver_ptr;

	files_cerver->start_result = cerver_start (files_cerver->cerver);

	return NULL;

}

// a NULL uploads path leaves the files cerver without one
static void files_cerver_start (
	FilesCerver *files_cerver,
	const char *name, const u16 port, const char *uploads_path
) {

	const unsigned int started = __atomic_load_n (&cerver_started, __ATOMIC_SEQ_CST);

	Cerver *cerver = cerver_create (
		CERVER_TYPE_FILES, name,
		port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, FILES_THREADS);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (cerver, FILES_POLL_TIMEOUT);

	FileCerver *file_cerver = (FileCerver *) cerver->cerver_data;
	test_check_ptr (file_cerver);

	test_check_unsigned_eq (file_cerver_add_path (file_cerver, FILES_SERVED_PATH), 0, NULL);
	file_cerver_set_uploads_path (file_cerver, uploads_path);
	file_cerver_set_file_upload_cb (file_cerver, files_cerver_upload_cb);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_STARTED,
		files_cerver_started, NULL, NULL,
		false, false
	);

	files_cerver->cerver = cerver;
	files_cerver->start_result = 0;

	test_check_int_eq (pthread_create (&files_cerver->thread, NULL, files_cerver_thread, files_cerver), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_started, started + 1, TEST_WAIT_SECONDS),
		started + 1, "the cerver did not start"
	);

}

static void files_cerver_stop (FilesCerver *files_cerver) {

	test_check_unsigned_eq (cerver_shutdown (files_cerver->cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += FILES_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (files_cerver->thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (files_cerver->start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (files_cerver->cerver), 0, NULL);

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

static void *files_client_send_file_error (void *error_data_ptr) {

	client_error_data_delete ((ClientErrorData *) error_data_ptr);

	test_counter_add (&send_file_errors);

	return NULL;

}

static Connection *files_client_connect (Client *client, const u16 port) {

	Connection *connection = client_connection_create (
		client, FILES_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);

	return connection;

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

	(void) client_error_register (
		client, CLIENT_ERROR_SEND_FILE,
		files_client_send_file_error, NULL, NULL,
		false, false
	);

	*connection = files_client_connect (client, FILES_PORT);

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

// a filename that leaves the served path is rejected, the file is never sent
static void test_files_get_outside (Client *client, Connection *connection) {

	const unsigned int uploads = __atomic_load_n (&client_uploads, __ATOMIC_SEQ_CST);

	test_check_unsigned_eq (client_file_get (client, connection, FILES_OUTSIDE_NAME), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&client_uploads, uploads + 1, FILES_OUTSIDE_WAIT_SECONDS),
		uploads, "a file outside of the served path was sent"
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

// a filename without a '/' (a file of the current directory) is sent with its whole name (it was rejected)
static void test_files_send_without_directory (Client *client, Connection *connection) {

	const unsigned int uploads = __atomic_load_n (&cerver_uploads, __ATOMIC_SEQ_CST);

	test_check_int_eq (chdir (FILES_SEND_PATH), 0, NULL);
	const u8 result = client_file_send (client, connection, FILES_SENT_NAME);
	test_check_int_eq (chdir (FILES_ROOT), 0, NULL);

	test_check_unsigned_eq (result, 0, "a filename without a directory was not sent");
	test_check_unsigned_eq (
		test_wait_counter (&cerver_uploads, uploads + 1, TEST_WAIT_SECONDS),
		uploads + 1, "the cerver did not save the file without a directory"
	);

	test_check (files_check (cerver_saved_filename, FILES_SIZE, FILES_SENT_SEED), "the saved file is different");

	test_check_unsigned_ne (client_file_send (client, connection, FILES_SEND_PATH "/"), 0);

}

// the bytes of the file follow its packet, so a cerver that does not take them
// ends the connection, they can not be read as packets
static void test_files_send_without_uploads_path (Client *client, FilesCerver *files_cerver) {

	const unsigned int uploads = __atomic_load_n (&cerver_uploads, __ATOMIC_SEQ_CST);

	Connection *connection = files_client_connect (client, FILES_WITHOUT_UPLOADS_PORT);

	// the cerver can end the connection before the last bytes of the file are sent, so the send can fail
	(void) client_file_send (client, connection, FILES_SEND_PATH "/" FILES_SENT_NAME);

	test_check_unsigned_eq (
		test_wait_counter (&send_file_errors, 1, TEST_WAIT_SECONDS),
		1, "the upload without an uploads path was not answered"
	);

	test_check_unsigned_eq (
		__atomic_load_n (&cerver_uploads, __ATOMIC_SEQ_CST), uploads, "a file was saved without an uploads path"
	);

	test_check (cerver_is_running (files_cerver->cerver), "the cerver stopped");

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing CLIENT files...\n");

	files_directories_create ();

	FilesCerver files_cerver = { NULL, 0, 0 };
	files_cerver_start (&files_cerver, FILES_NAME, FILES_PORT, FILES_CERVER_UPLOADS_PATH);

	FilesCerver without_uploads = { NULL, 0, 0 };
	files_cerver_start (&without_uploads, FILES_WITHOUT_UPLOADS_NAME, FILES_WITHOUT_UPLOADS_PORT, NULL);

	Connection *connection = NULL;
	Client *client = files_client_create (&connection);

	test_files_get_without_uploads_path (client, connection);
	test_files_get (client, connection);
	test_files_get_missing (client, connection);
	test_files_get_outside (client, connection);
	test_files_send (client, connection);
	test_files_send_without_directory (client, connection);
	test_files_send_without_uploads_path (client, &without_uploads);

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

	files_cerver_stop (&without_uploads);
	files_cerver_stop (&files_cerver);

	(void) printf ("Done with CLIENT files tests!\n\n");

	return 0;

}
