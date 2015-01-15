/*
 * protocol.h
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stdint.h>

#define PROTO_CONN_GREET10    1
#define PROTO_CONN_RESP41     2

typedef struct proto_str {
    uint16_t  len;
    uint16_t  sz;
    char     *data;
} proto_str_t;

typedef struct proto_conn_greet10 {
    uint8_t      pversion;
    proto_str_t  sversion;
    uint32_t     connid;
    proto_str_t  salt;
    uint32_t     capab_fs;
    uint8_t      charset;
    uint16_t     status_fs;
    proto_str_t  auth_plug_name;
} proto_conn_greet10_t;

typedef struct proto_conn_resp41 {
    uint32_t    capab_fs;
    uint32_t    max_packet_size;
    uint8_t     charset;

    proto_str_t username;
    proto_str_t password;
    proto_str_t schema;

    proto_str_t auth_plug_name;
    proto_str_t attr;

} proto_conn_resp41_t;

int proto_pack_read(void *evbuf, int ptype, void *pbody, size_t psize);

int proto_pack_write(void *evbuf, int ptype, void *pbody, size_t psize);

#endif /* PROTOCOL_H_ */
