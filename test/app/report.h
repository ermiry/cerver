#ifndef _TEST_APP_REPORT_H_
#define _TEST_APP_REPORT_H_

#include <cerver/serializer.h>

#define REPORT_NAME_SIZE			32
#define REPORT_VERSION				1
#define REPORT_REQUEST				1

// the value of the field that an older peer does not send
#define REPORT_EXTRA_DEFAULT		9

// the report of an older peer, without the field that the newer one added
#define REPORT_OLD_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, U32, id, SERIALIZER_REQUIRED, 0)								\
	STRING(T, 2, TEXT, name, REPORT_NAME_SIZE, SERIALIZER_REQUIRED)

#define REPORT_NEW_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	SCALAR(T, 1, U32, id, SERIALIZER_REQUIRED, 0)								\
	STRING(T, 2, TEXT, name, REPORT_NAME_SIZE, SERIALIZER_REQUIRED)			\
	SCALAR(T, 3, U32, extra, SERIALIZER_OPTIONAL, REPORT_EXTRA_DEFAULT)

SERIALIZER_DECLARE (ReportOld, report_old, REPORT_OLD_FIELDS)

SERIALIZER_DECLARE (ReportNew, report_new, REPORT_NEW_FIELDS)

#endif
