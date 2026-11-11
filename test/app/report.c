#include <stddef.h>

#include <cerver/serializer.h>

#include "report.h"

SERIALIZER_DEFINE (ReportOld, report_old, REPORT_VERSION, REPORT_OLD_FIELDS)

SERIALIZER_DEFINE (ReportNew, report_new, REPORT_VERSION, REPORT_NEW_FIELDS)
