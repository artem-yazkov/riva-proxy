#include <errno.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "aux.h"
#include "config.h"
#include "session.h"
#include "protocol.h"

#define ORDERSET_INC   (8)
typedef struct orderset {
    int       *fnums;
    int       *otypes;
    size_t     count;
    size_t     sz;
} orderset_t;
static orderset_t orderset;

#define ROWSET_INC     (64)
typedef struct rowset {
    MYSQL_ROW *rows;
    size_t     count;
    size_t     sz;
} rowset_t;
static rowset_t rowset;

static config_tbl_hdl_t *tblhdl;

static void
orderset_add(orderset_t *oset, int fnum, int otype)
{
    if (oset->count == oset->sz) {
        oset->sz += ORDERSET_INC;
        oset->fnums = realloc(oset->fnums, oset->sz * sizeof(oset->fnums[0]));
        oset->otypes = realloc(oset->otypes, oset->sz * sizeof(oset->otypes[0]));
    }
    oset->fnums[oset->count] = fnum;
    oset->otypes[oset->count] = otype;
    oset->count++;
}

static void
rowset_add(rowset_t *rset, MYSQL_ROW row)
{
    if (rset->count == rset->sz) {
        rset->sz += ROWSET_INC;
        rset->rows = realloc(rset->rows, rset->sz * sizeof(rset->rows[0]));
    }
    rset->rows[rset->count] = row;
    rset->count++;
}

static int
rowset_compare(const void *a, const void *b)
{
    MYSQL_ROW *parow = (void *)a;
    MYSQL_ROW *pbrow = (void *)b;
    MYSQL_ROW  arow = *parow;
    MYSQL_ROW  brow = *pbrow;
    for (int iord = 0; iord < orderset.count; iord++) {
        if (arow[orderset.fnums[iord]] == NULL) {
            return 1;
        }
        if (brow[orderset.fnums[iord]] == NULL) {
            return -1;
        }
        int cres = strcmp(arow[orderset.fnums[iord]], brow[orderset.fnums[iord]]);;
        if (cres != 0) {
            if (orderset.otypes[iord] == 0) {
                return cres;
            } else {
                return -cres;
            }
        }
    }

    return 0;
}

static int
query_parse_from(char *query, session_t *session)
{
    static char  *lquery;
    static size_t lquery_sz;

    if (lquery_sz < (strlen(query) + 1)) {
        lquery_sz = strlen(query) + 1;
        lquery = realloc(lquery, lquery_sz);
    }

    int q, lq;
    for (q = 0, lq = 0; query[q] != '\0'; q++) {
        if (!(isspace(query[q]) && isspace(query[q+1]))) {
            lquery[lq++] = tolower(query[q]);
        }
    }
    lquery[lq] = '\0';

    char fneedle[] = " from `";
    char *fstraw = lquery;
    fstraw = strstr(fstraw, fneedle);
    if (fstraw == NULL) {
        return -1;
    }
    char *tblname = fstraw + sizeof(fneedle) - 1;

    for (int ic = 0; (tblname[ic] != '\0'); ic++) {
        if (tblname[ic] == '`') {
            tblname[ic] = '\0';
            break;
        }
    }
    if (!config_tbl_search(tblname, &tblhdl)) {
        return -1;
    }

    return 0;
}

static int
query_parse(char *query, session_t *session)
{
    orderset.count = 0;

    static char  *lquery;
    static size_t lquery_sz;

    if (lquery_sz < (strlen(query) + 1)) {
        lquery_sz = strlen(query) + 1;
        lquery = realloc(lquery, lquery_sz);
    }

    int q, lq;
    for (q = 0, lq = 0; query[q] != '\0'; q++) {
        if (!(isspace(query[q]) && isspace(query[q+1]))) {
            lquery[lq++] = tolower(query[q]);
        }
    }
    lquery[lq] = '\0';

    char needle[] = "order by";
    char *straw = lquery;
    char *lstraw = NULL;
    while ((straw = strstr(straw, needle)) != NULL) {
        lstraw = straw++;
    }
    if (lstraw == NULL) {
        return -1;
    }

    char *fields = lstraw + strlen(needle);
    char *field = strtok(fields, ",");
    char *field_post = NULL;
    while (field != NULL) {
        if (isspace(field[0])) {
            field++;
        }
        char quote = '\0';
        if ((field[0] == '`') || (field[0] == '\'') || (field[0] == '"')) {
            quote = field[0];
            field++;
        }
        for (int ic = 0; field[ic]; ic++) {
            if ((quote && (field[ic] == quote)) || (!quote && isspace(field[ic]))) {
                field[ic] = '\0';
                field_post = &field[ic + 1];
                break;
            }
        }
        if (field[0] == '\0') {
            return -1;
        }

        if (isspace(field_post[0])) {
            field_post++;
        }

        MYSQL       *mconn;
        MYSQL_FIELD *mfield;
        MYSQL_RES   *mres;
        int          ifield = 0;
        int          fieldnum = -1;
        config_tbl_st_first(tblhdl);
        config_tbl_st_next(tblhdl, &mconn, (void**)&mres);

        mysql_field_seek(mres, 0);
        while ((mfield = mysql_fetch_field(mres))) {
            if (strcmp(mfield->name, field) == 0) {
                fieldnum = ifield;
                break;
            }
            ifield++;
        }
        if (fieldnum < 0) {
            return -1;
        }

        int ordertype = 0;
        if (strncmp(field_post, "desc", 4) == 0) {
            ordertype = 1;
        }

        orderset_add(&orderset, fieldnum, ordertype);

        field = strtok(NULL, ",");
        break;
    }
    return 0;
}

static void
__query_execute_err(char *errstr, int erridb, struct evbuffer *output, session_t *session)
{
    //aux_log(AUX_LT_WARN, "error at backend %s: %s", session->cfg->db[erridb].url, errstr);

    static proto_resp_err_t rerr;
    rerr.message.data = errstr;
    rerr.message.len = strlen(rerr.message.data);
    proto_pack_write(output, PROTO_RESP_ERR, &rerr, sizeof(rerr));

    MYSQL       *mconn;
    MYSQL_RES   *mres;

    config_tbl_st_first(tblhdl);
    while (config_tbl_st_next(tblhdl, &mconn, (void**)&mres)) {
        mysql_free_result(mres);
    }
}

static int
query_execute(struct evbuffer *output, char *query, session_t *session)
{
    static proto_resp_fcount_t rfcount;
    rfcount.fcount = 0;

    aux_log(AUX_LT_QUERY, "%s", query);

    if (query_parse_from(query, session) < 0) {
        return -1;
    }

    config_tbl_st_first(tblhdl);
    MYSQL       *mconn;
    MYSQL_RES   *mres;
    while (config_tbl_st_next(tblhdl, &mconn, (void**)&mres)) {
        int qresult = mysql_query(mconn, query);
        if (qresult == 0) {
            mres = mysql_store_result(mconn);
        }
        if (mysql_errno(mconn) > 0) {
            __query_execute_err((char *)mysql_error(mconn), 0, output, session);
            return -1;
        }
        if ((rfcount.fcount != 0) && (mysql_field_count(mconn) != rfcount.fcount)) {
            __query_execute_err("inconsistent field numbers across backends", 0, output, session);
            return -1;
        }
        rfcount.fcount = mysql_field_count(mconn);
        config_tbl_st_set_uptr(tblhdl, mres);
    }
    if (rfcount.fcount == 0) {
        static proto_resp_ok_t rok;
        rok.status_fl = 0x0002;
        proto_pack_write(output, PROTO_RESP_OK, &rok, sizeof(rok));
        return 0;
    }


    rowset.count = 0;
    config_tbl_st_first(tblhdl);
    while (config_tbl_st_next(tblhdl, &mconn, (void**)&mres)) {
        if (mres == NULL) {
            continue;
        }
        MYSQL_ROW  mrow;
        while ((mrow = mysql_fetch_row(mres))) {
            if ((session->cfg->limit) && (rowset.count > session->cfg->limit)) {
                break;
            }
            rowset_add(&rowset, mrow);
        }
    }

    query_parse(query, session);

    if (orderset.count > 0) {
        qsort(rowset.rows, rowset.count, sizeof(MYSQL_ROW), rowset_compare);
    }

    /*
     *
     * */
    proto_pack_write(output, PROTO_RESP_FCOUNT, &rfcount, sizeof(rfcount));

    MYSQL_FIELD *mfield;
    static proto_resp_field_t  rfield;
    config_tbl_st_first(tblhdl);
    config_tbl_st_next(tblhdl, &mconn, (void**)&mres);
    mysql_field_seek(mres, 0);
    while ((mfield = mysql_fetch_field(mres))) {
        rfield.catalog.data = mfield->catalog;
        rfield.catalog.len  = strlen(rfield.catalog.data);
        rfield.name.data    = mfield->name;
        rfield.name.len     = strlen(rfield.name.data);
        rfield.charset      = mfield->charsetnr;
        rfield.length       = mfield->length;
        rfield.type         = mfield->type;
        rfield.decimals     = mfield->decimals;
        proto_pack_write(output, PROTO_RESP_FIELD, &rfield, sizeof(rfield));
    }

    static proto_resp_eof_t reof;
    reof.status_fl = 0x0002;
    proto_pack_write(output, PROTO_RESP_EOF, &reof, sizeof(reof));

    static proto_resp_row_t rrow;
    rrow.values_cnt = rfcount.fcount;
    if (rrow.values_sz < rfcount.fcount) {
        rrow.values_sz = rfcount.fcount;
        rrow.values = realloc(rrow.values, rrow.values_sz * sizeof(proto_str_t));
        memset(rrow.values, 0, rrow.values_sz * sizeof(proto_str_t));
    }

    int rowlimit = (session->cfg->limit > 0) ? session->cfg->limit : rowset.count;
    for (int irow = 0; irow < rowlimit; irow++) {
        MYSQL_ROW  mrow = rowset.rows[irow];
        for(int icell = 0; icell < rfcount.fcount; icell++) {
            rrow.values[icell].data = mrow[icell];
            rrow.values[icell].len = (mrow[icell]) ? strlen(mrow[icell]) : 0;
        }
        proto_pack_write(output, PROTO_RESP_ROW, &rrow, sizeof(rrow));
    }
    proto_pack_write(output, PROTO_RESP_EOF, &reof, sizeof(reof));

    config_tbl_st_first(tblhdl);
    while (config_tbl_st_next(tblhdl, &mconn, (void**)&mres)) {
        if (mres == NULL) {
            continue;
        }
        mysql_free_result(mres);
    }

    aux_log(AUX_LT_STAT, "Fields: %lu, Rows: %d", rfcount.fcount, rowset.count);

    return rowset.count;
}

static void
cb_read(struct bufferevent *bev, void *ctx)
{
    session_t *session = ctx;

    /* This callback is invoked when there is data to read on bev. */
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    /* Connection phase: greet response */
    if (session->phase == SESSION_PHASE_CONN) {
        static proto_conn_resp41_t resp41;
        proto_pack_read(input, PROTO_CONN_RESP41, &resp41, sizeof(resp41));

        aux_log(AUX_LT_INFO, "resp41.capab_fs       : %X", resp41.capab_fs);
        aux_log(AUX_LT_INFO, "resp41.max_packet_size: %u", resp41.max_packet_size);
        aux_log(AUX_LT_INFO, "resp41.charset        : %u", resp41.charset);
        aux_log(AUX_LT_INFO, "resp41.username       : %s", resp41.username.data);
        aux_log(AUX_LT_INFO, "resp41.password.len   : %u", resp41.password.len);
        aux_log(AUX_LT_INFO, "presp.password        : %s", aux_dbg_hexprint(resp41.password.data, resp41.password.len));
        aux_log(AUX_LT_INFO, "resp41.schema         : %s", resp41.schema.data);
        aux_log(AUX_LT_INFO, "resp41.auth_plug_name : %s", resp41.auth_plug_name.data);
        aux_log(AUX_LT_INFO, "resp41.attr.data      : %s", aux_dbg_hexprint(resp41.attr.data, resp41.attr.len));

        static proto_resp_ok_t rok;
        rok.status_fl = 0x0002;
        proto_pack_write(output, PROTO_RESP_OK, &rok, sizeof(rok));
        session->phase = SESSION_PHASE_QUERY;
        return;
    }

    /* Query phase */
    if (session->phase == SESSION_PHASE_QUERY) {
        uint8_t rtype, psec;
        size_t  psize;

        if (proto_pack_look(input, &rtype, &psec, &psize) < 0) {
            return;
        }
        if (rtype == 3) {
            /* query */
            static proto_req_query_t rquery;
            proto_pack_read(input, PROTO_REQ_QUERY, &rquery, sizeof(rquery));
            if (rquery.query.len > 0) {
                query_execute(output, rquery.query.data, session);
            }
        } else if (rtype == 1) {
            /* quit */
            /* do nothing */
        } else {
            aux_log(AUX_LT_WARN, "Ignore unexpected packet: request type: %u, sequence: %u, payload size: %lu", rtype, psec, psize);
            evbuffer_drain(input, psize + 4);
            static proto_resp_eof_t reof;
            reof.status_fl = 0x0002;
            proto_pack_write(output, PROTO_RESP_EOF, &reof, sizeof(reof));
        }
    }
}

static void
cb_event(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR) {
        aux_log(AUX_LT_ERROR, "Error from bufferevent");
    }

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        session_t *session = ctx;
        free(session);
        bufferevent_free(bev);
        aux_log(AUX_LT_INFO, "close connection");
    }
}

void
session_accept_conn(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    session_t *session = calloc(1, sizeof(session_t));
    session->cfg = ctx;

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
           base, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(bev, cb_read, NULL, cb_event, session);

    bufferevent_enable(bev, EV_READ|EV_WRITE);

    static proto_conn_greet10_t pgreet;
    memset(&pgreet, 0, sizeof(pgreet));
    pgreet.pversion = 10;
    pgreet.sversion.data = "riva-proxy";
    pgreet.sversion.len = strlen(pgreet.sversion.data);
    pgreet.connid = 28;

    char salt[] =  {0x36, 0x53, 0x7e, 0x43, 0x71, 0x33, 0x46, 0x45, 0x69, 0x62,
                    0x2f, 0x5f, 0x74, 0x4c, 0x5a, 0x63, 0x60, 0x39, 0x3c, 0x61};
    pgreet.salt.data = salt;
    pgreet.salt.len = sizeof(salt);
    pgreet.capab_fs = 0xa03ff7ff;
    pgreet.status_fs = 0x0002;
    pgreet.charset = 33;
    pgreet.auth_plug_name.data = "mysql_native_password";
    pgreet.auth_plug_name.len = strlen(pgreet.auth_plug_name.data);
    proto_pack_write(bufferevent_get_output(bev), PROTO_CONN_GREET10, &pgreet, sizeof(pgreet));
}

void
session_accept_error(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    aux_log(AUX_LT_ERROR, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}
