#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerver/types/string.h>

#include <cerver/collections/avl.h>
#include <cerver/collections/dlist.h>

#include <cerver/game/score.h>

#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/threads/bsem.h>

#include <cerver/utils/utils.h>

#include "../test.h"
#include "core.h"
#include "faults.h"

// the allocations of the core that fail, with a fault injected in one of its calls

// the string before the token is the base name, its calloc () has its characters and the null byte
#define FAULTS_CORE_BASENAME			"cerver"
#define FAULTS_CORE_FILENAME			FAULTS_CORE_BASENAME ".txt"
#define FAULTS_CORE_TOKEN				'.'

// the types of the scoreboard are not the size of any other of its allocations
#define FAULTS_CORE_SCORE_PLAYERS		8
#define FAULTS_CORE_SCORE_TYPES			6

static void faults_core_handler (void *data) {

	(void) data;

}

static int faults_core_compare (const void *one, const void *two) {

	return (one > two) - (one < two);

}

// a list or a tree without memory for its mutex is not returned (it had a NULL mutex)
static void faults_core_collections (void) {

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (pthread_mutex_t));
	test_check_null_ptr (dlist_init (free, NULL));
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (pthread_mutex_t));
	test_check_null_ptr (avl_init (faults_core_compare, free));
	faults_disarm (FAULTS_FUNCTION_MALLOC);

}

static unsigned int faults_core_queue_triggered (void) {

	return faults_triggered (FAULTS_FUNCTION_MALLOC) + faults_triggered (FAULTS_FUNCTION_CALLOC);

}

// a handler whose job queue could not be created is not returned (it had a NULL queue)
static void faults_core_handler_create (void) {

	const unsigned int triggered = faults_core_queue_triggered ();

	// the compiler can merge the malloc () and the memset () of the queue in a calloc ()
	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (JobQueue));
	faults_arm (FAULTS_FUNCTION_CALLOC, true, sizeof (JobQueue));
	Handler *handler = handler_create (faults_core_handler);
	faults_disarm (FAULTS_FUNCTION_CALLOC);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_unsigned_eq (faults_core_queue_triggered (), triggered + 1, "the job queue did not fail");
	test_check_null_ptr (handler);

}

// a semaphore without memory for its condition keeps no mutex and its post does nothing (it used a NULL condition)
static void faults_core_bsem (void) {

	bsem *semaphore = bsem_new ();
	test_check_ptr (semaphore);

	faults_arm (FAULTS_FUNCTION_MALLOC, true, sizeof (pthread_cond_t));
	bsem_init (semaphore, 0);
	faults_disarm (FAULTS_FUNCTION_MALLOC);

	test_check_null_ptr (semaphore->mutex);
	test_check_null_ptr (semaphore->cond);

	bsem_post (semaphore);
	bsem_wait (semaphore);

	bsem_delete (semaphore);

}

// a copy without memory is NULL (it copied into a NULL string)
static void faults_core_c_string (void) {

	char name[] = FAULTS_CORE_FILENAME;

	faults_arm (FAULTS_FUNCTION_CALLOC, true, sizeof (FAULTS_CORE_BASENAME));
	char *removed = c_string_remove_sub_after_token (name, FAULTS_CORE_TOKEN, NULL);
	faults_disarm (FAULTS_FUNCTION_CALLOC);

	test_check_null_ptr (removed);

}

// a scoreboard without memory for its types is not returned (it wrote in a NULL array)
static void faults_core_score (void) {

	faults_arm (FAULTS_FUNCTION_CALLOC, true, FAULTS_CORE_SCORE_TYPES * sizeof (char *));
	ScoreBoard *board = game_score_create (
		FAULTS_CORE_SCORE_PLAYERS, FAULTS_CORE_SCORE_TYPES,
		"kills", "deaths", "assists", "points", "time", "level"
	);
	faults_disarm (FAULTS_FUNCTION_CALLOC);

	test_check_unsigned_eq (
		(unsigned int) (FAULTS_CORE_SCORE_TYPES * sizeof (char *) != sizeof (ScoreBoard)), 1,
		"the types have the size of the scoreboard"
	);

	test_check_null_ptr (board);

}

// a shrink that fails keeps the string (it lost its characters)
static void faults_core_string (void) {

	String *str = str_new ("cerver");
	test_check_ptr (str);

	faults_arm (FAULTS_FUNCTION_REALLOC, true, FAULTS_ANY_SIZE);
	str_remove_last_char (str);
	faults_disarm (FAULTS_FUNCTION_REALLOC);

	test_check_ptr (str->str);
	test_check_str_eq (str->str, "cerver", "a failed shrink changed the string");

	str_delete (str);

}

// bigger than the biggest class of buffers, so the data comes from malloc () and realloc ()
#define FAULTS_CORE_PACKET_SIZE			(((size_t) 1 << PACKET_DATA_MAX_EXPONENT) + 1)

static char faults_core_packet_data[FAULTS_CORE_PACKET_SIZE];

// a packet without the copy of its data is not created (it was returned empty),
// and a buffer that can not grow keeps its data (the packet lost it)
static void faults_core_packet (void) {

	faults_arm (FAULTS_FUNCTION_MALLOC, true, FAULTS_CORE_PACKET_SIZE);
	Packet *empty = packet_create (PACKET_TYPE_APP, 0, faults_core_packet_data, FAULTS_CORE_PACKET_SIZE);
	faults_disarm (FAULTS_FUNCTION_MALLOC);
	test_check_null_ptr (empty);

	Packet *packet = packet_create (PACKET_TYPE_APP, 0, faults_core_packet_data, FAULTS_CORE_PACKET_SIZE);
	test_check_ptr (packet);

	faults_arm (FAULTS_FUNCTION_REALLOC, true, FAULTS_ANY_SIZE);
	const u8 appended = packet_append_data (packet, faults_core_packet_data, FAULTS_CORE_PACKET_SIZE);
	faults_disarm (FAULTS_FUNCTION_REALLOC);

	test_check_unsigned_eq (appended, 1, NULL);
	test_check_ptr (packet->data);
	test_check_unsigned_eq ((unsigned int) packet->data_size, FAULTS_CORE_PACKET_SIZE, "the packet lost its data");

	packet_delete (packet);

}

// a copy that can not be allocated keeps the String (it freed it and copied into NULL)
static void faults_core_string_copy (void) {

	String *to = str_new (FAULTS_CORE_BASENAME);
	String *from = str_new (FAULTS_CORE_FILENAME);
	test_check_ptr (to);
	test_check_ptr (from);

	faults_arm (FAULTS_FUNCTION_CALLOC, true, sizeof (FAULTS_CORE_FILENAME));
	str_copy (to, from);
	faults_disarm (FAULTS_FUNCTION_CALLOC);

	test_check_str_eq (to->str, FAULTS_CORE_BASENAME, "a failed copy changed the String");

	str_delete (from);
	str_delete (to);

}

void faults_tests_core (void) {

	(void) printf ("Testing FAULTS core...\n");

	faults_core_collections ();
	faults_core_handler_create ();
	faults_core_bsem ();
	faults_core_c_string ();
	faults_core_score ();
	faults_core_string ();
	faults_core_string_copy ();
	faults_core_packet ();

	(void) printf ("Done!\n");

}
