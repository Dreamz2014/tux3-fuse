#ifndef USER_TRACE_H
#define USER_TRACE_H

#include "newDefines.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <execinfo.h>
#include <stdarg.h>
//#define die(code) exit(code)
#define die(code)	do { int *__p = NULL; *__p = code; } while (0)
#define assert(expr)	do {						\
	if (!(expr)) {							\
		if (ALLOW_BUILTIN_LOG==1)				\
			fprintf(stderr, "%s:%d: Failed assert(" #expr ")\n",	\
			__func__, __LINE__);				\
		die(99);						\
	}								\
} while (0)

#include "kernel/trace.h"

#endif /* !USER_TRACE_H */
