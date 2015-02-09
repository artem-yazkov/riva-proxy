#include "aux.h"

#include <stdio.h>
#include <stdlib.h>

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
