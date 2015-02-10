#include "aux.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

uint64_t aux_lt_mask = AUX_LT_DEFAULT;

char* aux_dbg_hexprint(char *data, size_t size)
{
    static char   *output;
    static size_t  outsize;
    if (outsize < size*3) {
        outsize = size*3;
        output = realloc(output, outsize);
    }
    int ichar;
    for (ichar = 0; ichar < size; ichar++) {
        sprintf(&output[ichar*3], "%02X ", data[ichar] & 0xff);
    }
    output[ichar*3 + 2] = '\0';
    return output;
}

void aux_log(int lt, char *format, ...)
{
    if (!(aux_lt_mask && lt)) {
        return;
    }

    char *ltname = "UNKNOWN";
    switch (lt) {
    case AUX_LT_ERROR: ltname = "ERROR"; break;
    case AUX_LT_WARN:  ltname = "WARN";  break;
    case AUX_LT_INFO:  ltname = "INFO";  break;
    case AUX_LT_STAT:  ltname = "STAT";  break;
    case AUX_LT_QUERY: ltname = "QUERY"; break;
    }

    struct tm *stm;
    struct timeval tim;
    gettimeofday(&tim, NULL );
    stm = localtime(&tim.tv_sec);
    printf("[%04d-%02d-%02d %02d:%02d:%02d.%05d] [%s] ",
            stm->tm_year + 1900,
            stm->tm_mon + 1,
            stm->tm_mday,
            stm->tm_hour,
            stm->tm_min,
            stm->tm_sec,
            (int)tim.tv_usec / 10,
            ltname);

    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
