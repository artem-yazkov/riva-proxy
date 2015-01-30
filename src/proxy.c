#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <mysql/mysql.h>

#include <arpa/inet.h>

#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "protocol.h"

#define CFG_LISTEN_HOST "0.0.0.0"
#define CFG_LISTEN_PORT 9876
#define CFG_ITEM_MAXLEN 64

typedef struct proxy_cfg_db {
    char database[CFG_ITEM_MAXLEN];
    char username[CFG_ITEM_MAXLEN];
    char password[CFG_ITEM_MAXLEN];
    char host    [CFG_ITEM_MAXLEN];
    int  port;
} proxy_cfg_db_t;

typedef struct proxy_cfg {
    char lhost   [CFG_ITEM_MAXLEN];
    int  lport;
    proxy_cfg_db_t *db;
    int             db_cnt;
    int             verbose;
} proxy_cfg_t;

#define PROXY_SESSPHASE_CONN    0
#define PROXY_SESSPHASE_QUERY   1
typedef struct proxy_session {
    MYSQL     **dbc;
    MYSQL_RES **mres;
    int         db_cnt;
    int         phase;
} proxy_session_t;

static proxy_cfg_t proxy_cfg;

char* __dbg_hexprint(char *data, size_t size)
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

static int
execute_query(struct evbuffer *output, char *query, proxy_session_t *session)
{
    static proto_resp_fcount_t rfcount;
    rfcount.fcount = 0;

    if (proxy_cfg.verbose) {
        fprintf(stdout, "Exec: %s\n", query);
    }

    for (int idb = 0; idb < session->db_cnt; idb++) {
        int qresult = mysql_query(session->dbc[idb], query);
        if (qresult == 0) {
            session->mres[idb] = mysql_store_result(session->dbc[idb]);
        }
        if (mysql_errno(session->dbc[idb]) > 0) {
            fprintf(stderr, "error at %d backend: %s\n",
                    idb, mysql_error(session->dbc[idb]));

            static proto_resp_err_t rerr;
            rerr.message.data = (char *)mysql_error(session->dbc[idb]);
            rerr.message.len = strlen(rerr.message.data);
            proto_pack_write(output, PROTO_RESP_ERR, &rerr, sizeof(rerr));
            return -1;
        }
        if ((rfcount.fcount != 0) && (mysql_field_count(session->dbc[idb]) != rfcount.fcount)) {
            static proto_resp_err_t rerr;
            fprintf(stderr, "inconsistent field numbers across backends");
            rerr.message.data = "inconsistent field numbers across backends";
            rerr.message.len = strlen(rerr.message.data);
            proto_pack_write(output, PROTO_RESP_ERR, &rerr, sizeof(rerr));
            return -1;
        }
        rfcount.fcount = mysql_field_count(session->dbc[idb]);
    }
    if (rfcount.fcount == 0) {
        static proto_resp_ok_t rok;
        rok.status_fl = 0x0002;
        proto_pack_write(output, PROTO_RESP_OK, &rok, sizeof(rok));
        return 0;
    }


    proto_pack_write(output, PROTO_RESP_FCOUNT, &rfcount, sizeof(rfcount));

    MYSQL_FIELD *mfield;
    static proto_resp_field_t  rfield;
    while ((mfield = mysql_fetch_field(session->mres[0]))) {
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

    int rcount = 0;
    for (int idb = 0; idb < session->db_cnt; idb++) {
        if (session->mres[idb] == NULL) {
            printf("continue\n");
            continue;
        }
        MYSQL_ROW  mrow;
        while ((mrow = mysql_fetch_row(session->mres[idb]))) {
            int irow;
            for(irow = 0; irow < rfcount.fcount; irow++) {
                rrow.values[irow].data = mrow[irow];
                rrow.values[irow].len = (mrow[irow]) ? strlen(mrow[irow]) : 0;
            }
            proto_pack_write(output, PROTO_RESP_ROW, &rrow, sizeof(rrow));
            rcount++;
        }
        mysql_free_result(session->mres[idb]);
    }

    proto_pack_write(output, PROTO_RESP_EOF, &reof, sizeof(reof));

    if (proxy_cfg.verbose) {
        fprintf(stdout, "Fields: %lu, Rows: %d\n", rfcount.fcount, rcount);
    }
    return rcount;
}

static void
cb_read(struct bufferevent *bev, void *ctx)
{
    proxy_session_t *session = ctx;

    /* This callback is invoked when there is data to read on bev. */
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    /* Connection phase: greet response */
    if (session->phase == PROXY_SESSPHASE_CONN) {
        static proto_conn_resp41_t resp41;
        proto_pack_read(input, PROTO_CONN_RESP41, &resp41, sizeof(resp41));
        if (proxy_cfg.verbose) {
            printf("resp41.capab_fs       : %X\n", resp41.capab_fs);
            printf("resp41.max_packet_size: %u\n", resp41.max_packet_size);
            printf("resp41.charset        : %u\n", resp41.charset);
            printf("resp41.username       : %s\n", resp41.username.data);
            printf("resp41.password.len   : %u\n", resp41.password.len);
            printf("presp.password        : %s\n", __dbg_hexprint(resp41.password.data, resp41.password.len));
            printf("resp41.schema         : %s\n", resp41.schema.data);
            printf("resp41.auth_plug_name : %s\n", resp41.auth_plug_name.data);
            printf("resp41.attr.data      : %s\n", __dbg_hexprint(resp41.attr.data, resp41.attr.len));
        }

        static proto_resp_ok_t rok;
        rok.status_fl = 0x0002;
        proto_pack_write(output, PROTO_RESP_OK, &rok, sizeof(rok));
        session->phase = PROXY_SESSPHASE_QUERY;
        return;
    }

    /* Query phase */
    if (session->phase == PROXY_SESSPHASE_QUERY) {
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
                execute_query(output, rquery.query.data, session);
            }
        } else if (rtype == 1) {
            /* quit */
            /* do nothing */
        } else {
            if (proxy_cfg.verbose) {
                printf("Ignore unexpected packet: request type: %u, sequence: %u, payload size: %lu\n", rtype, psec, psize);
            }
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
        perror("Error from bufferevent");
    }

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        proxy_session_t *session = ctx;
        for (int idb = 0; idb < session->db_cnt; idb++) {
            mysql_close(session->dbc[idb]);
        }
        free(session->dbc);
        free(session);
        bufferevent_free(bev);
        printf("close connection\n");
    }
}

static void
cb_accept_conn(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    proxy_session_t *session = calloc(1, sizeof(proxy_session_t));
    session->db_cnt = proxy_cfg.db_cnt;
    session->dbc = calloc(session->db_cnt, sizeof(session->dbc[0]));
    session->mres = calloc(session->db_cnt, sizeof(session->mres[0]));

    for (int idb = 0; idb < session->db_cnt; idb++) {
        session->dbc[idb] = mysql_init(NULL);
        MYSQL *rconn = mysql_real_connect(session->dbc[idb],
                proxy_cfg.db[idb].host,
                proxy_cfg.db[idb].username,
               (proxy_cfg.db[idb].password[0] != '\n') ? proxy_cfg.db[idb].password : NULL,
                proxy_cfg.db[idb].database,
                proxy_cfg.db[idb].port,
                NULL, 0);
        if (rconn == NULL) {
            fprintf(stderr, "Can't connect to %s:%s@%s:%d/%s\n",
                    proxy_cfg.db[idb].username,
                    proxy_cfg.db[idb].password,
                    proxy_cfg.db[idb].host,
                    proxy_cfg.db[idb].port,
                    proxy_cfg.db[idb].database);
            fprintf(stderr, "Close connection\n");
            event_base_loopexit(evconnlistener_get_base(listener), NULL);
        }
    }

    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
           base, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(bev, cb_read, NULL, cb_event, session);

    bufferevent_enable(bev, EV_READ|EV_WRITE);

    static proto_conn_greet10_t pgreet;
    memset(&pgreet, 0, sizeof(pgreet));
    pgreet.pversion = 10;
    pgreet.sversion.data = "5.5.5-10.0.14-MariaDB-log";
    pgreet.sversion.len = strlen(pgreet.sversion.data);
    pgreet.connid = 28;

    char salt[20] =  {0x36, 0x53, 0x7e, 0x43, 0x71, 0x33, 0x46, 0x45, 0x69, 0x62,
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

static void
cb_accept_error(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

void
__help(char *errstr)
{
    if (errstr != NULL) {
        printf("%s\n", errstr);
    }
}

static void
__process_args(int argc, char * const argv[])
{
    strncpy(proxy_cfg.lhost, CFG_LISTEN_HOST, sizeof(proxy_cfg.lhost) - 1);
    proxy_cfg.lport = CFG_LISTEN_PORT;

    static struct option options[] = {
        /* name, has_arg, flag, value */
        { .name = "help"         , no_argument       , NULL , 'h' },
        { .name = "version"      , no_argument       , NULL , 'v' },
        { .name = "verbose"      , no_argument       , NULL , 'V' },
        { .name = "proxy-address", required_argument , NULL , 'a' },
        { .name = "backend"      , required_argument , NULL , 'b' },
        { NULL                   , 0                 , NULL ,  0  }
    };
    int opt;

    while ((opt = getopt_long(argc, argv, "hva:b:", options, NULL)) != -1) {
        if (opt == 'a') {
            int args;
            /* all arguments */
            args = sscanf(optarg, "%[^:]:%d", proxy_cfg.lhost, &proxy_cfg.lport);
            if (args == 2) {
                continue;
            }
            /* skip host */
            args = sscanf(optarg, ":%d", &proxy_cfg.lport);
            if (args == 1) {
                continue;
            }
            /* skip port */
            args = sscanf(optarg, "%[^:]", proxy_cfg.lhost);
            if (args == 1) {
                continue;
            }
            __help("Incorrect proxy-address");
            exit(1);
        }
        if (opt == 'b') {
            proxy_cfg.db_cnt++;
            proxy_cfg.db = realloc(proxy_cfg.db, sizeof(proxy_cfg_db_t) * proxy_cfg.db_cnt);

            proxy_cfg_db_t *db = &proxy_cfg.db[proxy_cfg.db_cnt - 1];
            memset(db, 0, sizeof(proxy_cfg_db_t));

            int args;
            /* all arguments */
            args = sscanf(optarg, "%[^:]:%[^@]@%[^:]:%d/%s", db->username, db->password,
                          db->host, &db->port, db->database);
            if (args == 5) {
                continue;
            }
            /* default port */
            args = sscanf(optarg, "%[^:]:%[^@]@%[^/]/%s", db->username, db->password,
                          db->host, db->database);
            if (args == 4) {
                continue;
            }
            /* default password */
            args = sscanf(optarg, "%[^@]@%[^:]:%d/%s", db->username,
                          db->host, &db->port, db->database);
            if (args == 5) {
                continue;
            }
            /* default password, port */
            args = sscanf(optarg, "%[^@]@%[^/]/%s", db->username, db->host, db->database);
            if (args == 3) {
                continue;
            }
            /* incorrect pattern */
            __help("Incorrect backend");
            exit(1);
        }
        if (opt == 'h') {
            __help(NULL);
        }
        if (opt == 'V') {
            proxy_cfg.verbose = 1;
        }
    }
    if (proxy_cfg.db_cnt == 0) {
        __help("At least one backend must be specified");
        exit(1);
    }
}

int
main(int argc, char **argv)
{
    __process_args(argc, argv);

    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;

    base = event_base_new();
    if (!base) {
        puts("Couldn't open event base");
        return 1;
    }

    /* Clear the sockaddr before using it, in case there are extra
     * platform-specific fields that can mess us up. */
    memset(&sin, 0, sizeof(sin));
    /* This is an INET address */
    sin.sin_family = AF_INET;
    /* Listen on given ip4 address */
    sin.sin_addr.s_addr = inet_addr(proxy_cfg.lhost);
    /* Listen on the given port. */
    sin.sin_port = htons(proxy_cfg.lport);

    listener = evconnlistener_new_bind(base, cb_accept_conn, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
        perror("Couldn't create listener");
        return 1;
    }
    evconnlistener_set_error_cb(listener, cb_accept_error);

    event_base_dispatch(base);
    return 0;
}
