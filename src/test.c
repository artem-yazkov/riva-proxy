#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

const char resp41hex[] = "                       \
8d a6 3f 20 00 00 00 01 21 00 00 00 00 00 00 00  \
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  \
61 72 74 65 6d 00 14 92 9c 0c ce 70 ad 4a 32 89  \
5d 37 39 28 a4 9f 9d 5c 2b 63 a8 72 69 76 61 73  \
65 6e 73 65 00 6d 79 73 71 6c 5f 6e 61 74 69 76  \
65 5f 70 61 73 73 77 6f 72 64 00 67 03 5f 6f 73  \
05 4c 69 6e 75 78 0c 5f 63 6c 69 65 6e 74 5f 6e  \
61 6d 65 08 6c 69 62 6d 79 73 71 6c 04 5f 70 69  \
64 05 32 30 36 31 30 0f 5f 63 6c 69 65 6e 74 5f  \
76 65 72 73 69 6f 6e 07 31 30 2e 30 2e 31 34 09  \
5f 70 6c 61 74 66 6f 72 6d 06 78 38 36 5f 36 34  \
0c 70 72 6f 67 72 61 6d 5f 6e 61 6d 65 05 6d 79  \
73 71 6c";

char* dbg_hexprint(char *data, size_t size)
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

int main()
{
    proto_conn_greet10_t pgreet;
    memset(&pgreet, 0, sizeof(pgreet));
    pgreet.pversion = 10;
    pgreet.sversion.data = "5.5.5-10.0.14-MariaDB-log";
    pgreet.sversion.len = strlen(pgreet.sversion.data);
    pgreet.connid = 28;

    char salt[20] =  {0x36, 0x53, 0x7e, 0x43, 0x71, 0x33, 0x46, 0x45, 0x69, 0x62,
                      0x2f, 0x5f, 0x74, 0x4c, 0x5a, 0x63, 0x60, 0x39, 0x3c, 0x61};
    pgreet.salt.data = salt;
    pgreet.salt.len = strlen(pgreet.salt.data);
    pgreet.capab_fs = 0xa03ff7ff;
    pgreet.status_fs = 0x0002;
    pgreet.charset = 33;
    pgreet.auth_plug_name.data = "mysql_native_password";
    pgreet.auth_plug_name.len = strlen(pgreet.auth_plug_name.data);
    proto_pack_write(NULL, PROTO_CONN_GREET10, &pgreet, sizeof(pgreet));

    printf("\n");

    proto_conn_resp41_t presp;
    memset(&presp, 0, sizeof(presp));
    proto_pack_read((char *)resp41hex, PROTO_CONN_RESP41, &presp, sizeof(presp));
    printf("presp.capab_fs       : %X\n", presp.capab_fs);
    printf("presp.max_packet_size: %d\n", presp.max_packet_size);
    printf("presp.charset:         %d\n", presp.charset);
    printf("presp.username:        %s\n", presp.username.data);
    printf("presp.password.len:    %d\n", presp.password.len);
    printf("presp.password:        %s\n", dbg_hexprint(presp.password.data, presp.password.len));
    printf("presp.schema:          %s\n", presp.schema.data);

    return 0;
}
