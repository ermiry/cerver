#ifndef _CERVER_CONFIG_H_
#define _CERVER_CONFIG_H_

#define CERVER_EXPORT 			extern	// to be used in the application
#define CERVER_PRIVATE			extern	// to be used by internal cerver methods
#define CERVER_PUBLIC 			extern	// used by private methods but can be used by the application

#define DEPRECATED(func) func __attribute__ ((deprecated))

#endif