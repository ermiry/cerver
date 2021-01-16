#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/client.h>

#include "../test.h"

static const char *client_name = "test-client";

static Client *test_client_create (void) {

	Client *client = client_create ();

	test_check_str_eq (client->name->str, "no-name", NULL);
	test_check_str_len (client->name, strlen ("no-name"), NULL);
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
	test_check_str_eq (client->name->str, client_name, NULL);
	test_check_str_len (client->name->str, strlen (client_name), NULL);

	client_delete (client);

}

int main (int argc, char **argv) {

	srand ((unsigned) time (NULL));

	(void) printf ("Testing CLIENT...\n");

	test_client_base_configuration ();

	(void) printf ("\nDone with CLIENT tests!\n\n");

	return 0;

}