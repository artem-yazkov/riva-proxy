/*
 * protocol.h
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

/* Types of packets */
#define PROTO_CONN_GREET10    1
#define PROTO_CONN_RESP41     2
#define PROTO_REQ_QUERY       3
#define PROTO_RESP_OK         4
#define PROTO_RESP_ERR        5
#define PROTO_RESP_EOF        6
#define PROTO_RESP_FCOUNT     7
#define PROTO_RESP_FIELD      8
#define PROTO_RESP_ROW        9

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
    proto_str_t *values;
} proto_resp_row_t;

int proto_pack_read(void *evbuf, int ptype, void *pbody, size_t psize);

int proto_pack_write(void *evbuf, int ptype, void *pbody, size_t psize);

#endif /* PROTOCOL_H_ */
