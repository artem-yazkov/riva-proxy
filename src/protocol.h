/*
 * protocol.h
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>
#include <event2/buffer.h>

/* Types of packets */
#define PROTO_TYPE_ALL        0xFFFF
#define PROTO_TYPE_REQ        0xFF00

#define PROTO_CONN_GREET10    0x0001
#define PROTO_CONN_RESP41     0x0002
#define PROTO_RESP_OK         0x0010
#define PROTO_RESP_ERR        0x0020
#define PROTO_RESP_EOF        0x0030
#define PROTO_RESP_FCOUNT     0x0040
#define PROTO_RESP_FIELD      0x0050
#define PROTO_RESP_ROW        0x0060
#define PROTO_REQ_QUERY       0x0300

#include "protocol-flags.h"

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

typedef struct proto_req_query {
    proto_str_t query;

} proto_req_query_t;

typedef struct proto_resp_ok {
    uint64_t    affected_rows;
    uint64_t    last_insert_id;
    uint16_t    status_fl;
    uint16_t    warnings;
    proto_str_t info;
    proto_str_t sess_changes;
} proto_resp_ok_t;

typedef struct proto_resp_err {
    uint16_t    err_code;
    char        sql_state[5];
    proto_str_t message;
} proto_resp_err_t;

typedef struct proto_resp_eof {
    uint16_t    warnings;
    uint16_t    status_fl;
} proto_resp_eof_t;

typedef struct proto_resp_fcount {
    uint64_t fcount;
} proto_resp_fcount_t;

typedef struct proto_resp_field {
    proto_str_t catalog;
    proto_str_t schema;
    proto_str_t table;
    proto_str_t org_table;
    proto_str_t name;
    proto_str_t org_name;
    uint16_t    charset;
    uint32_t    length;
    uint8_t     type;
    uint16_t    flags;
    uint8_t     decimals;
} proto_resp_field_t;

typedef struct proto_resp_row {
    size_t       values_cnt;
    size_t       values_sz;
    proto_str_t *values;
} proto_resp_row_t;

int proto_pack_look(struct evbuffer *evbuf, uint8_t *rtype, uint8_t *psec, size_t *psize);

int proto_pack_read(struct evbuffer *evbuf, int ptype, void *pbody, size_t psize);

int proto_pack_write(struct evbuffer *evbuf, int ptype, void *pbody, size_t psize);

#endif /* PROTOCOL_H_ */
