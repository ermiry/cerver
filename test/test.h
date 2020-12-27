#ifndef _CERVER_TESTS_H_
#define _CERVER_TESTS_H_

#include <stdio.h>

#define where fprintf (stderr, "%s - %d: ", __FILE__, __LINE__)

#define											\
	fail(msg) 									\
	({ 											\
		where; 									\
		fprintf (stderr, "%s\n", msg); 			\
		exit (1); 								\
	})

#endif