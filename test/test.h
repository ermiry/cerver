#ifndef _CERVER_TESTS_H_
#define _CERVER_TESTS_H_

#include <stdlib.h>
#include <stdio.h>

#define where (void) fprintf (stderr, "%s:%d -> ", __FILE__, __LINE__)

#define											\
	fail(msg) 									\
	({ 											\
		where; 									\
		(void) fprintf (stderr, "%s\n", msg); 	\
		exit (1); 								\
	})

#define																\
	test_check(result, msg)											\
	({																\
		if ((result) == false) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"Check has failed!\n"								\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define test_check_ptr_eq(X, Y) test_check (X == Y, NULL)

#define test_check_ptr_ne(X, Y) test_check (X != Y, NULL)

#define test_check_int_ne(X, Y) test_check (X != Y, NULL)

#define test_check_int_gt(X, Y) test_check (X > Y, NULL)

#define																\
	test_check_int_eq(value, expected, msg)							\
	({																\
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %d does not match %d\n",					\
				value, expected										\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_long_int_eq(value, expected, msg)					\
	({																\
		if (value != expected) {									\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"INTEGER %ld does not match %ld\n",					\
				value, expected										\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#define																\
	test_check_str_eq(value, expected, msg)							\
	({																\
		if (strcmp ((char *) value, (char *) expected)) {			\
			where;													\
			(void) fprintf (										\
				stderr,												\
				"STRING %s does not match %s\n",					\
				(char *) value, (char *) expected					\
			);														\
																	\
			if (msg) (void) fprintf (stderr, "%s\n", (char *) msg);	\
			exit (1);												\
		}															\
	})

#endif