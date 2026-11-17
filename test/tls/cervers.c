#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerver/auth/auth.h>
#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
#include <cerver/client/requests.h>
#include <cerver/connection/connection.h>
#include <cerver/connection/data.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/files/cerver.h>
#include <cerver/files/files.h>
#include <cerver/handler/events.h>
#include <cerver/handler/handler.h>
#include <cerver/load/balancer.h>
#include <cerver/load/end.h>
#include <cerver/load/service.h>
#include <cerver/load/start.h>
#include <cerver/packets/packets.h>

#include <cerver/tls/cerver.h>
#include <cerver/tls/client.h>
#include <cerver/tls/tls.h>

#include "../test.h"
#include "tls.h"

// the other types of cervers with TLS, with the clients of cerver in the same process:
// - a FILES cerver sends a file to a client (encrypted by cerver, or by the kernel with kTLS and sendfile ())
// and saves the file that a client uploads (file_receive_decrypted ()), with the same bytes, in POLL and THREADS
// - a BALANCER with TLS towards its clients and towards its services routes the packets both ways
// - a CUSTOM cerver with auth_required: the connection is put on hold after its handshake, it authenticates
// and its packets are echoed; a wrong token is dropped, in POLL and THREADS

// the ports after the ones of the providers test
#define CERVERS_PORT_BASE				(TLS_TEST_PORT_BASE + (1 << 9))

#define CERVERS_ADDRESS					"127.0.0.1"

#define CERVERS_THREADS					4
#define CERVERS_POLL_TIMEOUT			200
#define CERVERS_JOIN_SECONDS			8

// a deadline for the handshakes, the clients of these tests never leave one in the middle
#define CERVERS_HANDSHAKE_TIMEOUT		(1 << 11)

// the root, a folder of the root and a file of a folder, each one fits in the next one
#define CERVERS_ROOT_SIZE				64
#define CERVERS_FOLDER_SIZE				(CERVERS_ROOT_SIZE * 2)
#define CERVERS_PATH_SIZE				512
#define CERVERS_DIRECTORY_MODE			0700
#define CERVERS_DIRECTORY_TEMPLATE		"/tmp/cerver-tls-cervers-XXXXXX"

#define CERVERS_SERVED_FOLDER			"served"
#define CERVERS_CERVER_UPLOADS_FOLDER	"cerver-uploads"
#define CERVERS_CLIENT_UPLOADS_FOLDER	"client-uploads"
#define CERVERS_SEND_FOLDER				"send"

#define CERVERS_SERVED_NAME				"served.bin"
#define CERVERS_SENT_NAME				"sent.bin"

// more than the records of 2^14 and the mark of 2^16 of a send, and not a multiple of them
#define CERVERS_FILE_SIZE				((1 << 20) + (1 << 10) + 3)
#define CERVERS_BYTE_PATTERN			0xFF
#define CERVERS_SERVED_SEED				0
#define CERVERS_SENT_SEED				7

#define CERVERS_REQUEST_ECHO			1
#define CERVERS_PACKETS					32
#define CERVERS_BODY_SIZE				(1 << 10)
#define CERVERS_MAX_PACKET_SIZE			(1 << 16)

#define CERVERS_SERVICES				2
#define CERVERS_RECONNECT_SECONDS		1

#define CERVERS_AUTH_TOKEN_SIZE			64
#define CERVERS_AUTH_TOKEN				"tls-on-hold-token"
#define CERVERS_AUTH_WRONG_TOKEN		"tls-wrong-token"
#define CERVERS_AUTH_MAX_TRIES			1

typedef struct CerversCerver {

	Cerver *cerver;
	pthread_t thread;
	u8 start_result;

	unsigned int started;
	unsigned int packets;

} CerversCerver;

typedef struct CerversClient {

	Client *client;
	Connection *connection;

} CerversClient;

typedef struct CerversToken {

	char token[CERVERS_AUTH_TOKEN_SIZE];

} CerversToken;

typedef struct CerversFolders {

	char root[CERVERS_ROOT_SIZE];
	char served[CERVERS_FOLDER_SIZE];
	char cerver_uploads[CERVERS_FOLDER_SIZE];
	char client_uploads[CERVERS_FOLDER_SIZE];
	char send[CERVERS_FOLDER_SIZE];
	char sent_file[CERVERS_PATH_SIZE];

} CerversFolders;

static u16 cervers_port = CERVERS_PORT_BASE;

static CerversFolders folders;

static unsigned int echoes = 0;
static unsigned int bad_echoes = 0;

static unsigned int cerver_uploads = 0;
static unsigned int client_uploads = 0;

static unsigned int on_hold_connections = 0;
static unsigned int success_auths = 0;
static unsigned int failed_auths = 0;

static char cerver_saved_filename[CERVERS_PATH_SIZE] = { 0 };
static char client_saved_filename[CERVERS_PATH_SIZE] = { 0 };

static void cervers_counters_reset (void) {

	__atomic_store_n (&echoes, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&bad_echoes, 0, __ATOMIC_SEQ_CST);

	__atomic_store_n (&cerver_uploads, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&client_uploads, 0, __ATOMIC_SEQ_CST);

	__atomic_store_n (&on_hold_connections, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&success_auths, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&failed_auths, 0, __ATOMIC_SEQ_CST);

}

#pragma region bytes

static char cervers_byte (const size_t index, const unsigned int seed) {

	return (char) ((index + seed) & CERVERS_BYTE_PATTERN);

}

static void cervers_body_fill (char *body, const size_t size) {

	for (size_t index = 0; index < size; index++) body[index] = cervers_byte (index, 0);

}

static bool cervers_body_check (const char *body, const size_t size) {

	bool valid = (size == CERVERS_BODY_SIZE);
	for (size_t index = 0; valid && (index < size); index++) valid = (body[index] == cervers_byte (index, 0));

	return valid;

}

static void cervers_file_create (const char *filename, const size_t size, const unsigned int seed) {

	FILE *file = fopen (filename, "wb");
	test_check_ptr (file);

	for (size_t index = 0; index < size; index++) (void) fputc (cervers_byte (index, seed), file);

	(void) fclose (file);

}

static bool cervers_file_check (const char *filename, const size_t size, const unsigned int seed) {

	bool valid = false;

	FILE *file = fopen (filename, "rb");
	if (file) {
		valid = true;

		size_t index = 0;
		int c = 0;
		while (valid && ((c = fgetc (file)) != EOF)) {
			valid = (index < size) && ((char) c == cervers_byte (index, seed));
			index += 1;
		}

		if (index != size) valid = false;

		(void) fclose (file);
	}

	return valid;

}

static void cervers_folder (char *path, const char *folder) {

	(void) snprintf (path, CERVERS_FOLDER_SIZE, "%s/%s", folders.root, folder);
	test_check_int_eq (mkdir (path, CERVERS_DIRECTORY_MODE), 0, NULL);

}

static void cervers_folders_create (void) {

	(void) snprintf (folders.root, CERVERS_ROOT_SIZE, "%s", CERVERS_DIRECTORY_TEMPLATE);
	test_check_ptr (mkdtemp (folders.root));

	cervers_folder (folders.served, CERVERS_SERVED_FOLDER);
	cervers_folder (folders.cerver_uploads, CERVERS_CERVER_UPLOADS_FOLDER);
	cervers_folder (folders.client_uploads, CERVERS_CLIENT_UPLOADS_FOLDER);
	cervers_folder (folders.send, CERVERS_SEND_FOLDER);

	char served_file[CERVERS_PATH_SIZE];
	(void) snprintf (served_file, sizeof (served_file), "%s/%s", folders.served, CERVERS_SERVED_NAME);
	cervers_file_create (served_file, CERVERS_FILE_SIZE, CERVERS_SERVED_SEED);

	(void) snprintf (folders.sent_file, CERVERS_PATH_SIZE, "%s/%s", folders.send, CERVERS_SENT_NAME);
	cervers_file_create (folders.sent_file, CERVERS_FILE_SIZE, CERVERS_SENT_SEED);

}

static void cervers_folders_delete (void) {

	test_check_unsigned_eq (files_remove_recursive (folders.root), 0, NULL);

}

#pragma endregion

#pragma region cerver

static void *cervers_started (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void *cervers_count_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static TlsSettings *cervers_server_settings (const TestCertificates *certificates) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);

	test_check_unsigned_eq (
		tls_settings_add_certificate (settings, certificates->server_chain, certificates->server_key), 0, NULL
	);

	tls_settings_set_handshake_timeout_milliseconds (settings, test_milliseconds (CERVERS_HANDSHAKE_TIMEOUT));

	return settings;

}

// the address of the connection is verified in the IP of the certificate
static TlsSettings *cervers_client_settings (const TestCertificates *certificates) {

	TlsSettings *settings = tls_settings_create ();
	test_check_ptr (settings);

	test_check_unsigned_eq (tls_settings_set_ca (settings, certificates->authority, NULL), 0, NULL);

	return settings;

}

static Cerver *cervers_cerver_create (
	const CerverType type, const char *name, const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = cerver_create (type, name, port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE);
	test_check_ptr (cerver);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, CERVERS_THREADS);
	cerver_set_handler_type (cerver, handler_type);
	cerver_set_poll_time_out (cerver, CERVERS_POLL_TIMEOUT);
	cerver_set_max_received_packet_size (cerver, CERVERS_MAX_PACKET_SIZE);

	return cerver;

}

static void *cervers_cerver_thread (void *server_ptr) {

	CerversCerver *server = (CerversCerver *) server_ptr;

	server->start_result = cerver_start (server->cerver);

	return NULL;

}

// sets TLS in the cerver, starts it in its thread and waits for its event
static void cervers_cerver_start (CerversCerver *server, Cerver *cerver, TlsSettings *settings) {

	server->cerver = cerver;
	server->start_result = 0;
	server->started = 0;
	server->packets = 0;

	test_check_unsigned_eq (cerver_set_tls (cerver, settings), 0, NULL);
	test_check_true (cerver_has_tls (cerver));

	(void) cerver_event_register (
		cerver, CERVER_EVENT_STARTED,
		cervers_started, &server->started, NULL,
		false, false
	);

	test_check_int_eq (pthread_create (&server->thread, NULL, cervers_cerver_thread, server), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&server->started, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

}

static void cervers_join (const pthread_t thread) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += CERVERS_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (thread, NULL, &deadline), 0, "the start did not return");

}

static TlsStats cervers_cerver_stats (const Cerver *cerver) {

	TlsStats stats;
	test_check_unsigned_eq (cerver_tls_get_stats (cerver, &stats), 0, NULL);

	return stats;

}

// the data of a CUSTOM cerver of these tests is its CerversCerver, the one of a FILES cerver is released with it
static void cervers_cerver_stop (CerversCerver *server) {

	test_check_unsigned_eq (cerver_shutdown (server->cerver), 0, NULL);

	cervers_join (server->thread);
	test_check_unsigned_eq (server->start_result, 0, NULL);

	if (server->cerver->type == CERVER_TYPE_CUSTOM) server->cerver->cerver_data = NULL;
	test_check_unsigned_eq (cerver_teardown (server->cerver), 0, NULL);
	server->cerver = NULL;

}

// the response goes with the route id of the request, so a balancer routes it back
static void cervers_echo (const Packet *packet) {

	Packet *response = packet_generate_request (
		PACKET_TYPE_APP, packet->header.request_type,
		packet->data, packet->data_size
	);

	test_check_ptr (response);

	packet_set_route_id (response, packet->header.route_id);
	packet_set_network_values (response, packet->cerver, packet->client, packet->connection, NULL);

	(void) packet_send (response, 0, NULL, false);

	packet_delete (response);

}

static void cervers_cerver_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	CerversCerver *server = (CerversCerver *) packet->cerver->cerver_data;
	if (server) test_counter_add (&server->packets);

	if (packet->header.request_type == CERVERS_REQUEST_ECHO) cervers_echo (packet);

}

// a CUSTOM cerver that echoes the requests, it finds its CerversCerver in its data, never deleted by the cerver
static Cerver *cervers_echo_cerver_create (
	CerversCerver *server, const char *name, const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = cervers_cerver_create (CERVER_TYPE_CUSTOM, name, port, handler_type);
	cerver->cerver_data = server;

	Handler *app_handler = handler_create (cervers_cerver_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (cerver, app_handler, NULL);

	return cerver;

}

#pragma endregion

#pragma region client

static void cervers_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (cervers_body_check ((const char *) packet->data, packet->data_size)) test_counter_add (&echoes);
	else test_counter_add (&bad_echoes);

}

static Client *cervers_client_create (const TestCertificates *certificates) {

	Client *client = client_create ();
	test_check_ptr (client);

	client_set_max_received_packet_size (client, CERVERS_MAX_PACKET_SIZE);
	test_check_unsigned_eq (client_set_tls (client, cervers_client_settings (certificates)), 0, NULL);

	Handler *app_handler = handler_create (cervers_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (client, app_handler, NULL);

	return client;

}

static Connection *cervers_client_connection (Client *client, const u16 port) {

	Connection *connection = client_connection_create (client, CERVERS_ADDRESS, port, PROTOCOL_TCP, false);
	test_check_ptr (connection);

	return connection;

}

static void cervers_client_start (const CerversClient *client) {

	test_check_int_eq (client_connect_and_start (client->client, client->connection), 0, "failed to connect");

}

static CerversClient cervers_client_connect (const TestCertificates *certificates, const u16 port) {

	CerversClient client = { NULL, NULL };

	client.client = cervers_client_create (certificates);
	client.connection = cervers_client_connection (client.client, port);
	cervers_client_start (&client);

	return client;

}

static void cervers_client_send (const CerversClient *client) {

	char body[CERVERS_BODY_SIZE];
	cervers_body_fill (body, CERVERS_BODY_SIZE);

	Packet *packet = packet_generate_request (PACKET_TYPE_APP, CERVERS_REQUEST_ECHO, body, CERVERS_BODY_SIZE);
	test_check_ptr (packet);

	packet_set_network_values (packet, NULL, client->client, client->connection, NULL);
	test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

	packet_delete (packet);

}

static void cervers_client_check_echoes (const CerversClient *client, const unsigned int count) {

	const unsigned int expected = __atomic_load_n (&echoes, __ATOMIC_SEQ_CST) + count;

	for (unsigned int index = 0; index < count; index++) cervers_client_send (client);

	test_check_unsigned_eq (
		test_wait_counter (&echoes, expected, TEST_WAIT_SECONDS),
		expected, "the responses did not arrive"
	);

	test_check_unsigned_eq (__atomic_load_n (&bad_echoes, __ATOMIC_SEQ_CST), 0, "a response had other bytes");

}

static TlsStats cervers_client_stats (const CerversClient *client) {

	TlsStats stats;
	test_check_unsigned_eq (client_tls_get_stats (client->client, &stats), 0, NULL);

	return stats;

}

static void cervers_client_end (CerversClient *client) {

	if (client_is_running (client->client)) (void) client_connection_end (client->client, client->connection);

	test_check_unsigned_eq (client_teardown (client->client), 0, NULL);
	client->client = NULL;
	client->connection = NULL;

}

#pragma endregion

#pragma region files

static void cervers_files_cerver_upload (
	Cerver *cerver, Client *client, Connection *connection, const char *saved_filename
) {

	(void) cerver;
	(void) client;
	(void) connection;

	(void) snprintf (cerver_saved_filename, CERVERS_PATH_SIZE, "%s", saved_filename);

	test_counter_add (&cerver_uploads);

}

static void cervers_files_client_upload (Client *client, Connection *connection, const char *saved_filename) {

	(void) client;
	(void) connection;

	(void) snprintf (client_saved_filename, CERVERS_PATH_SIZE, "%s", saved_filename);

	test_counter_add (&client_uploads);

}

static void cervers_files_cerver_start (
	CerversCerver *server, const TestCertificates *certificates,
	const u16 port, const CerverHandlerType handler_type, const bool ktls
) {

	Cerver *cerver = cervers_cerver_create (CERVER_TYPE_FILES, "tls-files", port, handler_type);

	FileCerver *file_cerver = (FileCerver *) cerver->cerver_data;
	test_check_ptr (file_cerver);

	test_check_unsigned_eq (file_cerver_add_path (file_cerver, folders.served), 0, NULL);
	file_cerver_set_uploads_path (file_cerver, folders.cerver_uploads);
	file_cerver_set_file_upload_cb (file_cerver, cervers_files_cerver_upload);

	TlsSettings *settings = cervers_server_settings (certificates);
	tls_settings_set_ktls (settings, ktls);

	cervers_cerver_start (server, cerver, settings);

}

static void cervers_files_get (const CerversClient *client) {

	const unsigned int expected = __atomic_load_n (&client_uploads, __ATOMIC_SEQ_CST) + 1;

	test_check_unsigned_eq (client_file_get (client->client, client->connection, CERVERS_SERVED_NAME), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&client_uploads, expected, TEST_WAIT_SECONDS),
		expected, "the file did not arrive"
	);

	test_check (
		cervers_file_check (client_saved_filename, CERVERS_FILE_SIZE, CERVERS_SERVED_SEED),
		"the received file is different"
	);

}

static void cervers_files_send (const CerversClient *client) {

	const unsigned int expected = __atomic_load_n (&cerver_uploads, __ATOMIC_SEQ_CST) + 1;

	test_check_unsigned_eq (client_file_send (client->client, client->connection, folders.sent_file), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_uploads, expected, TEST_WAIT_SECONDS),
		expected, "the cerver did not save the file"
	);

	test_check (
		cervers_file_check (cerver_saved_filename, CERVERS_FILE_SIZE, CERVERS_SENT_SEED),
		"the saved file is different"
	);

}

static void cervers_files_check (
	const TestCertificates *certificates, const CerverHandlerType handler_type, const bool ktls
) {

	cervers_counters_reset ();

	const u16 port = cervers_port++;

	CerversCerver server;
	cervers_files_cerver_start (&server, certificates, port, handler_type, ktls);

	CerversClient client = { NULL, NULL };
	client.client = cervers_client_create (certificates);
	client_files_set_uploads_path (client.client, folders.client_uploads);
	client_files_set_file_upload_cb (client.client, cervers_files_client_upload);
	client.connection = cervers_client_connection (client.client, port);
	cervers_client_start (&client);

	// twice each way, in the same connection: what follows a file is read as packets again
	cervers_files_get (&client);
	cervers_files_send (&client);
	cervers_files_get (&client);
	cervers_files_send (&client);

	test_check_unsigned_eq ((unsigned int) cervers_client_stats (&client).handshakes, 1, NULL);

	cervers_client_end (&client);

	const TlsStats stats = cervers_cerver_stats (server.cerver);
	test_check_unsigned_eq ((unsigned int) stats.handshakes, 1, NULL);

	// the kernel can refuse kTLS (without the module tls), then cerver encrypts and the bytes are the same
	if (!ktls) test_check_unsigned_eq ((unsigned int) stats.ktls_send, 0, NULL);
	(void) printf ("\t\tkTLS %s: %u sends of the kernel\n", ktls ? "on" : "off", (unsigned int) stats.ktls_send);

	cervers_cerver_stop (&server);

}

static void test_cervers_files_poll (const TestCertificates *certificates) {

	cervers_files_check (certificates, CERVER_HANDLER_TYPE_POLL, false);
	cervers_files_check (certificates, CERVER_HANDLER_TYPE_POLL, true);

}

static void test_cervers_files_threads (const TestCertificates *certificates) {

	cervers_files_check (certificates, CERVER_HANDLER_TYPE_THREADS, false);
	cervers_files_check (certificates, CERVER_HANDLER_TYPE_THREADS, true);

}

#pragma endregion

#pragma region balancer

typedef struct CerversBalancer {

	Balancer *balancer;
	u16 port;

	pthread_t thread;
	u8 start_result;

	unsigned int started;

	CerversCerver services[CERVERS_SERVICES];
	u16 service_ports[CERVERS_SERVICES];

} CerversBalancer;

static void *cervers_balancer_thread (void *balancer_ptr) {

	CerversBalancer *balancer = (CerversBalancer *) balancer_ptr;

	balancer->start_result = balancer_start (balancer->balancer);

	return NULL;

}

static void cervers_balancer_services_start (CerversBalancer *balancer, const TestCertificates *certificates) {

	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) {
		CerversCerver *service = &balancer->services[index];
		Cerver *cerver = cervers_echo_cerver_create (
			service, "tls-service", balancer->service_ports[index], CERVER_HANDLER_TYPE_THREADS
		);

		cervers_cerver_start (service, cerver, cervers_server_settings (certificates));
	}

}

static void cervers_balancer_create (CerversBalancer *balancer, const TestCertificates *certificates) {

	balancer->port = cervers_port++;
	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) balancer->service_ports[index] = cervers_port++;

	balancer->balancer = balancer_create (
		"tls-balancer", BALANCER_TYPE_ROUND_ROBIN,
		balancer->port, CERVER_DEFAULT_CONNECTION_QUEUE,
		CERVERS_SERVICES
	);

	test_check_ptr (balancer->balancer);

	cerver_set_reusable_address_flags (balancer->balancer->cerver, true);
	cerver_set_max_received_packet_size (balancer->balancer->cerver, CERVERS_MAX_PACKET_SIZE);
	client_set_max_received_packet_size (balancer->balancer->client, CERVERS_MAX_PACKET_SIZE);

	// TLS towards the clients of the balancer and towards its services
	test_check_unsigned_eq (
		cerver_set_tls (balancer->balancer->cerver, cervers_server_settings (certificates)), 0, NULL
	);

	test_check_unsigned_eq (
		balancer_set_services_tls (balancer->balancer, cervers_client_settings (certificates)), 0, NULL
	);

	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) {
		test_check_unsigned_eq (
			balancer_service_register (balancer->balancer, CERVERS_ADDRESS, balancer->service_ports[index]),
			0, NULL
		);

		balancer_service_set_reconnect_wait_time (balancer->balancer->services[index], CERVERS_RECONNECT_SECONDS);
	}

	(void) cerver_event_register (
		balancer->balancer->cerver, CERVER_EVENT_STARTED,
		cervers_started, &balancer->started, NULL,
		false, false
	);

}

static void cervers_balancer_start (CerversBalancer *balancer, const TestCertificates *certificates) {

	cervers_balancer_create (balancer, certificates);
	cervers_balancer_services_start (balancer, certificates);

	balancer->started = 0;
	balancer->start_result = 0;

	test_check_int_eq (pthread_create (&balancer->thread, NULL, cervers_balancer_thread, balancer), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&balancer->started, 1, TEST_WAIT_SECONDS),
		1, "the balancer did not start"
	);

	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) {
		test_check_int_eq (
			balancer_service_get_status (balancer->balancer->services[index]), SERVICE_STATUS_WORKING,
			"a service with TLS is not working"
		);
	}

}

static void cervers_balancer_stop (CerversBalancer *balancer) {

	test_check_unsigned_eq (cerver_shutdown (balancer->balancer->cerver), 0, NULL);
	cervers_join (balancer->thread);
	test_check_unsigned_eq (balancer->start_result, 0, NULL);

	test_check_unsigned_eq (balancer_teardown (balancer->balancer), 0, NULL);
	balancer->balancer = NULL;

	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) cervers_cerver_stop (&balancer->services[index]);

}

static void test_cervers_balancer (const TestCertificates *certificates) {

	cervers_counters_reset ();

	CerversBalancer balancer;
	(void) memset (&balancer, 0, sizeof (CerversBalancer));
	cervers_balancer_start (&balancer, certificates);

	// the connections of the balancer to its services made their handshakes
	TlsStats services_stats;
	test_check_unsigned_eq (client_tls_get_stats (balancer.balancer->client, &services_stats), 0, NULL);
	test_check_true (services_stats.handshakes >= CERVERS_SERVICES);

	CerversClient client = cervers_client_connect (certificates, balancer.port);
	cervers_client_check_echoes (&client, CERVERS_PACKETS);

	// round robin used every service
	for (unsigned int index = 0; index < CERVERS_SERVICES; index++) {
		test_check_unsigned_gt (__atomic_load_n (&balancer.services[index].packets, __ATOMIC_SEQ_CST), 0);
		test_check_true (cervers_cerver_stats (balancer.services[index].cerver).handshakes >= 1);
	}

	test_check_unsigned_eq ((unsigned int) cervers_client_stats (&client).handshakes, 1, NULL);
	test_check_true (cervers_cerver_stats (balancer.balancer->cerver).handshakes >= 1);

	cervers_client_end (&client);

	cervers_balancer_stop (&balancer);

}

#pragma endregion

#pragma region auth

static u8 cervers_auth_method (void *auth_method_ptr) {

	const AuthMethod *auth_method = (const AuthMethod *) auth_method_ptr;
	const AuthData *auth_data = auth_method->auth_data;

	u8 retval = 1;
	if (auth_data && (auth_data->auth_data_size >= sizeof (CerversToken))) {
		const CerversToken *token = (const CerversToken *) auth_data->auth_data;
		retval = strncmp (token->token, CERVERS_AUTH_TOKEN, CERVERS_AUTH_TOKEN_SIZE) ? 1 : 0;
	}

	return retval;

}

static CerversToken *cervers_token_new (const char *value) {

	CerversToken *token = (CerversToken *) calloc (1, sizeof (CerversToken));
	test_check_ptr (token);

	(void) snprintf (token->token, CERVERS_AUTH_TOKEN_SIZE, "%s", value);

	return token;

}

static void cervers_auth_cerver_start (
	CerversCerver *server, const TestCertificates *certificates,
	const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = cervers_echo_cerver_create (server, "tls-auth", port, handler_type);

	test_check_unsigned_eq (cerver_set_auth (cerver, CERVERS_AUTH_MAX_TRIES, cervers_auth_method), 0, NULL);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_ON_HOLD_CONNECTED, cervers_count_event, &on_hold_connections, NULL, false, false
	);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_SUCCESS_AUTH, cervers_count_event, &success_auths, NULL, false, false
	);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_FAILED_AUTH, cervers_count_event, &failed_auths, NULL, false, false
	);

	cervers_cerver_start (server, cerver, cervers_server_settings (certificates));

}

static CerversClient cervers_auth_client_create (
	const TestCertificates *certificates, const u16 port, const char *token
) {

	CerversClient client = { NULL, NULL };

	client.client = cervers_client_create (certificates);
	client.connection = cervers_client_connection (client.client, port);

	connection_set_auth_data (client.connection, cervers_token_new (token), sizeof (CerversToken), free, false);

	return client;

}

static void cervers_auth_check (const TestCertificates *certificates, const CerverHandlerType handler_type) {

	cervers_counters_reset ();

	const u16 port = cervers_port++;

	CerversCerver server;
	cervers_auth_cerver_start (&server, certificates, port, handler_type);

	// the token goes encrypted to the on hold poll, and then the packets to the handler of the cerver
	CerversClient client = cervers_auth_client_create (certificates, port, CERVERS_AUTH_TOKEN);
	cervers_client_start (&client);

	test_check_unsigned_eq (test_wait_counter (&success_auths, 1, TEST_WAIT_SECONDS), 1, "the client did not authenticate");
	cervers_client_check_echoes (&client, CERVERS_PACKETS);
	cervers_client_end (&client);

	// a wrong token is dropped after its only try, its packets are never handled
	const unsigned int handled = __atomic_load_n (&server.packets, __ATOMIC_SEQ_CST);
	CerversClient wrong = cervers_auth_client_create (certificates, port, CERVERS_AUTH_WRONG_TOKEN);
	cervers_client_start (&wrong);

	test_check_unsigned_eq (test_wait_counter (&failed_auths, 1, TEST_WAIT_SECONDS), 1, "the wrong token was accepted");
	cervers_client_end (&wrong);

	test_check_unsigned_eq (__atomic_load_n (&server.packets, __ATOMIC_SEQ_CST), handled, NULL);
	test_check_unsigned_eq (__atomic_load_n (&on_hold_connections, __ATOMIC_SEQ_CST), 2, NULL);
	test_check_unsigned_eq (__atomic_load_n (&success_auths, __ATOMIC_SEQ_CST), 1, NULL);
	test_check_true (cervers_cerver_stats (server.cerver).handshakes >= 2);

	cervers_cerver_stop (&server);

}

static void test_cervers_auth_poll (const TestCertificates *certificates) {

	cervers_auth_check (certificates, CERVER_HANDLER_TYPE_POLL);

}

static void test_cervers_auth_threads (const TestCertificates *certificates) {

	cervers_auth_check (certificates, CERVER_HANDLER_TYPE_THREADS);

}

#pragma endregion

void tls_tests_cervers (const TestCertificates *certificates) {

	(void) printf ("Testing TLS cervers...\n");

	cervers_folders_create ();

	(void) printf ("\ttest_cervers_files_poll\n");
	test_cervers_files_poll (certificates);
	(void) printf ("\ttest_cervers_files_threads\n");
	test_cervers_files_threads (certificates);

	(void) printf ("\ttest_cervers_balancer\n");
	test_cervers_balancer (certificates);

	(void) printf ("\ttest_cervers_auth_poll\n");
	test_cervers_auth_poll (certificates);
	(void) printf ("\ttest_cervers_auth_threads\n");
	test_cervers_auth_threads (certificates);

	cervers_folders_delete ();

	(void) printf ("Done!\n");

}
