/*
 * protocol.c
 */

#include "protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct __buffer {
    char   *data;
    size_t  dlen;
    size_t  dsize;
    size_t  cursor;
} __buffer_t;

#define BUFFER_INC_SIZE       4096
#define BUFFER_FIELD_INC_SIZE   64

static uint64_t sequence_id;

static void
__buffer_dump(__buffer_t *buf, size_t offset)
{
    int ichar, icolumn = 0;
    for (ichar = 0; ichar < (buf->dlen + offset); ichar++) {
        if (ichar < offset) {
            printf("   ");
        } else {
            printf("%02X ", buf->data[ichar - offset] & 0xff);
        }

        icolumn++;
        if (icolumn == 8) {
            printf(" ");
        } else if (icolumn == 16) {
            printf("\n");
            icolumn = 0;
        }
    }
}

static void
__buffer_restore(__buffer_t *buf, char *dump)
{
    buf->cursor = buf->dlen = 0;
    while (*dump && !isalnum(*dump)) {
        dump++;
    }

    while (*dump) {
        if (buf->dlen == buf->dsize) {
            buf->dsize += BUFFER_INC_SIZE;
            buf->data = realloc(buf->data, buf->dsize);
        }
        sscanf(dump, "%X", &buf->data[buf->dlen++]);
        while (*dump && isalnum(*dump)) {
            dump++;
        }
        while (*dump && !isalnum(*dump)) {
            dump++;
        }
    }
}

static inline int
__field_read(__buffer_t *buf, void *fbody, size_t fsize)
{
    if (buf->cursor + fsize > buf->dlen) {
        return -1;
    }
    if (fbody != NULL) {
        memcpy(fbody, buf->data + buf->cursor, fsize);
    }
    buf->cursor += fsize;
    return 0;
}

static inline void
__field_read_str(__buffer_t *buf, proto_str_t *str, size_t fsize)
{
    int isnullstr = (fsize == 0);
    if (fsize == 0) {
        for (; (buf->cursor + fsize <= buf->dlen) && buf->data[buf->cursor + fsize];
                fsize++);
    }
    if (str->sz < fsize+1) {
        str->sz = fsize+1;
        str->data = realloc(str->data, str->sz);
    }
    str->len = fsize;
    __field_read(buf, str->data, str->len);
    str->data[fsize] = '\0';

    if (isnullstr) {
        __field_read(buf, NULL, 1);
    }
}

static inline void
__field_read_lenenc(__buffer_t *buf, uint64_t *val)
{
    __field_read(buf, val, 1);
    if (*val < 0xfb) {
        return;

    } else if (*val == 0xfc) {
        __field_read(buf, val, 2);

    } else if (*val == 0xfd) {
        __field_read(buf, val, 3);

    } else {
        __field_read(buf, val, 8);
    }
}

static inline void
__field_write(__buffer_t *buf, void *fbody, size_t fsize)
{
    if (buf->cursor + fsize > buf->dsize) {
        buf->dsize += BUFFER_INC_SIZE;
        buf->data = realloc(buf->data, buf->dsize);
    }
    if (fbody != NULL) {
        memcpy(buf->data + buf->cursor, fbody, fsize);
    } else {
        memset(buf->data + buf->cursor, 0, fsize);
    }
    buf->cursor += fsize;
    buf->dlen = buf->cursor;
}

static inline void
__field_write_lenenc(__buffer_t *buf, uint64_t val)
{
    if (val < 0xfb) {
        __field_write(buf, &val, 1);

    } else if (val < 0xffff) {
        char flag = 0xfc;
        __field_write(buf, &flag, 1);
        __field_write(buf, &val,  2);

    } else if (val < 0xffffff) {
        char flag = 0xfd;
        __field_write(buf, &flag, 1);
        __field_write(buf, &val,  3);

    } else {
        char flag = 0xfe;
        __field_write(buf, &flag, 1);
        __field_write(buf, &val,  8);

    }
}

static int
__writepack_conn_greet10(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_conn_greet10_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }

    __field_write(buf, &p->pversion, 1);
    __field_write(buf,  p->sversion.data, p->sversion.len + 1);
    __field_write(buf, &p->connid, 4);
    __field_write(buf,  p->salt.data, 8);
    __field_write(buf,  NULL, 1);

    __field_write(buf, ((char *)(&p->capab_fs) + 0), 2);
    __field_write(buf, &p->charset, 1);
    __field_write(buf, &p->status_fs, 2);
    __field_write(buf, ((char *)(&p->capab_fs) + 2), 2);

    __field_write(buf, &p->auth_plug_name.len, 1);

    __field_write(buf, NULL, 10);
    __field_write(buf, &p->salt.data[8], (p->salt.len - 8));
    if (13 - (p->salt.len - 8) > 0) {
        __field_write(buf, NULL, 13 - (p->salt.len - 8));
    }

    __field_write(buf, p->auth_plug_name.data, p->auth_plug_name.len+1);

    return 0;
}

static int
__readpack_conn_resp41(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_conn_resp41_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }

    __field_read(buf, &p->capab_fs, 4);
    __field_read(buf, &p->max_packet_size, 4);
    __field_read(buf, &p->charset, 1);
    __field_read(buf, NULL, 23);

    __field_read_str(buf, &p->username, 0);
    __field_read(buf, &p->password.len, 1);
    __field_read_str(buf, &p->password, p->password.len);
    __field_read_str(buf, &p->schema, 0);

    return 0;
}

static int
__readpack_req_query(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_req_query_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }
    __field_read_str(buf, &p->query, 0);
    return 0;
}

static int
__writepack_resp_ok(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_ok_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }

    char header = 0x00;
    __field_write(buf, &header, 1);
    __field_write_lenenc(buf, p->affected_rows);
    __field_write_lenenc(buf, p->last_insert_id);
    __field_write(buf, &p->status_fl, 2);
    __field_write(buf, &p->warnings, 2);

    return 0;
}

static int
__writepack_resp_eof(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_eof_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }

    char header = 0xfe;
    __field_write(buf, &header, 1);
    __field_write(buf, &p->warnings, 2);
    __field_write(buf, &p->status_fl, 2);

    return 0;
}

static int
__writepack_resp_err(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_err_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }

    char header = 0xff;
    char marker = '#';
    __field_write(buf, &header, 1);
    __field_write(buf, &p->err_code, 2);
    __field_write(buf, &marker, 1);
    __field_write(buf, &p->sql_state, 5);
    __field_read_str(buf, &p->message, 0);

    return 0;
}

static int
__writepack_resp_fcount(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_fcount_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }
    __field_write_lenenc(buf, p->fcount);
    return 0;
}

static int
__writepack_resp_field(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_field_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }
    __field_write_lenenc(buf, p->catalog.len);
    __field_write(buf, p->catalog.data, p->catalog.len);
    __field_write_lenenc(buf, p->schema.len);
    __field_write(buf, p->schema.data, p->schema.len);
    __field_write_lenenc(buf, p->table.len);
    __field_write(buf, p->table.data, p->table.len);
    __field_write_lenenc(buf, p->org_table.len);
    __field_write(buf, p->org_table.data, p->org_table.len);
    __field_write_lenenc(buf, p->name.len);
    __field_write(buf, p->name.data, p->name.len);
    __field_write_lenenc(buf, p->org_name.len);
    __field_write(buf, p->org_name.data, p->org_name.len);

    char marker = 0x0c;
    __field_write(buf, &marker, 1);
    __field_write(buf, &p->charset, 2);
    __field_write(buf, &p->length, 4);
    __field_write(buf, &p->type, 1);
    __field_write(buf, &p->flags, 2);
    __field_write(buf, &p->decimals, 1);
    __field_write(buf, NULL, 2);

    return 0;
}

static int
__writepack_resp_row(__buffer_t *buf, void *pbody, size_t psize)
{
    proto_resp_row_t *p = pbody;
    if (psize != sizeof(*p)) {
        return -1;
    }
    int ival;
    for (ival = 0; ival < p->values_cnt; ival++) {
        __field_write_lenenc(buf, p->values[ival].len);
        __field_write(buf, p->values[ival].data, p->values[ival].len);
    }
    return 0;
}

int proto_pack_look(struct evbuffer *evbuf, uint8_t *rtype, uint8_t *psec, size_t *psize)
{
    uint8_t phdr[5];
    if (evbuffer_get_length(evbuf) < sizeof(phdr)) {
        return -1;
    }
    evbuffer_copyout(evbuf, phdr, sizeof(phdr));
    memcpy(psize, phdr, 3);
    *psec = phdr[3];
    *rtype = phdr[4];

    if (evbuffer_get_length(evbuf) < *psize + 5) {
        return -1;
    }

    return 0;
}

int proto_pack_read(struct evbuffer *evbuf, int ptype, void *pbody, size_t psize)
{
    if (evbuffer_get_length(evbuf) < 5) {
        return -1;
    }
    static __buffer_t buffer;
    buffer.cursor = buffer.dlen = 0;

    evbuffer_remove(evbuf, &buffer.dlen, 3);
    evbuffer_remove(evbuf, &sequence_id, 1);

    if (buffer.dlen < buffer.dsize) {
        buffer.dsize = buffer.dlen;
        buffer.data = realloc(evbuf, buffer.dsize);
    }
    evbuffer_remove(evbuf, &buffer.data, buffer.dlen);

    if (ptype == PROTO_CONN_RESP41) {
        return __readpack_conn_resp41(&buffer, pbody, psize);
    }
    if (ptype == PROTO_REQ_QUERY) {
        __field_read(&buffer, NULL, 1);
        return __readpack_req_query(&buffer, pbody, psize);
    }

    return -1;
}

int proto_pack_write(struct evbuffer *evbuf, int ptype, void *pbody, size_t psize)
{
    static __buffer_t buffer;
    buffer.cursor = buffer.dlen = 0;

    switch (ptype) {
    case PROTO_CONN_GREET10:
        sequence_id = 0;
        __writepack_conn_greet10(&buffer, pbody, psize);
        //__buffer_dump(&buffer, 6);
        //printf("\n");
        break;
    case PROTO_RESP_EOF:
        __writepack_resp_eof(&buffer, pbody, psize);
        break;
    case PROTO_RESP_ERR:
        __writepack_resp_err(&buffer, pbody, psize);
        break;
    case PROTO_RESP_OK:
        __writepack_resp_ok(&buffer, pbody, psize);
        break;
    case PROTO_RESP_FCOUNT:
        __writepack_resp_fcount(&buffer, pbody, psize);
        break;
    case PROTO_RESP_FIELD:
        __writepack_resp_field(&buffer, pbody, psize);
        break;
    case PROTO_RESP_ROW:
        __writepack_resp_row(&buffer, pbody, psize);
        break;
    default:
        return -1;
    }

    evbuffer_add(evbuf, &buffer.dlen, 3);
    evbuffer_add(evbuf, &sequence_id, 1);
    evbuffer_add(evbuf, buffer.data, buffer.dlen);
    sequence_id++;

    return 0;
}
