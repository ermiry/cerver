#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/connection.h>

#include "test.h"

static const char *connection_name = "test-connection";

static Connection *test_connection_create (void) {

	Connection *connection = connection_create_empty ();

	test_check_str_eq (connection->name, CONNECTION_DEFAULT_NAME, NULL);
	test_check_str_len (connection->name, strlen (CONNECTION_DEFAULT_NAME), NULL);
	test_check_unsigned_eq (connection->connected_timestamp, 0, NULL);
	test_check_ptr (connection->socket);
	test_check_ptr (connection->stats);

	return connection;

}

static void test_connection_base_configuration (void) {

	Connection *connection = test_connection_create ();

	connection_set_name (connection,connection_name);
	test_check_str_eq (connection->name, connection_name, NULL);
	test_check_str_len (connection->name, strlen (connection_name), NULL);

	connection_delete (connection);

}

int main (int argc, char **argv) {

	(void) printf ("Testing CONNECTION...\n");

	test_connection_base_configuration ();

	(void) printf ("\nDone with CONNECTION tests!\n\n");

	return 0;

}