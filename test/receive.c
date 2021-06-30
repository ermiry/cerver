#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/receive.h>

#include "test.h"

static void test_receive_error_to_string (void) {

	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_NONE), "None", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_TIMEOUT), "Timeoout", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_EMPTY), "Empty", NULL);
	test_check_str_eq (receive_error_to_string (RECEIVE_ERROR_FAILED), "Failed", NULL);

	test_check_str_eq (receive_error_to_string ((ReceiveError) 10), "None", NULL);

}

static void test_receive_type_to_string (void) {
	
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_NONE), "None", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_NORMAL), "Normal", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_ON_HOLD), "On-Hold", NULL);
	test_check_str_eq (receive_type_to_string (RECEIVE_TYPE_ADMIN), "Admin", NULL);

	test_check_str_eq (receive_type_to_string ((ReceiveType) 10), "None", NULL);

}

static void test_receive_handle_state_to_string (void) {
	
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_NONE), "None", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_NORMAL), "Normal", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_SPLIT_HEADER), "Split-Header", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_SPLIT_PACKET), "Split-Packet", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_COMP_HEADER), "Complete-Header", NULL);
	test_check_str_eq (receive_handle_state_to_string (RECEIVE_HANDLE_STATE_LOST), "Lost", NULL);

	test_check_str_eq (receive_handle_state_to_string ((ReceiveHandleState) 10), "None", NULL);

}

static void test_receive_handle_create (void) {

	ReceiveHandle *receive = receive_handle_new ();

	test_check_ptr (receive);

	test_check_unsigned_eq (receive->type, RECEIVE_TYPE_NONE, NULL);

	test_check_null_ptr (receive->cerver);

	test_check_null_ptr (receive->socket);
	test_check_null_ptr (receive->connection);
	test_check_null_ptr (receive->client);
	test_check_null_ptr (receive->admin);

	test_check_null_ptr (receive->lobby);

	test_check_null_ptr (receive->buffer);
	test_check_unsigned_eq (receive->buffer_size, 0, NULL);

	test_check_unsigned_eq (receive->state, RECEIVE_HANDLE_STATE_NONE, NULL);

	test_check_null_ptr (receive->header_end);
	test_check_unsigned_eq (receive->remaining_header, 0, NULL);

	test_check_null_ptr (receive->spare_packet);

	receive_handle_delete (receive);

}

int main (int argc, char **argv) {

	(void) printf ("Testing RECEIVE HANDLE...\n");

	test_receive_error_to_string ();
	test_receive_type_to_string ();
	test_receive_handle_state_to_string ();

	test_receive_handle_create ();

	(void) printf ("\nDone with RECEIVE HANDLE tests!\n\n");

	return 0;

}
