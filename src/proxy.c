#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <mysql/mysql.h>

#include <arpa/inet.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include "protocol.h"

static  MYSQL *mconn0;
static  MYSQL *mconn1;

static void
echo_read_cb(struct bufferevent *bev, void *ctx)
{
    /* This callback is invoked when there is data to read on bev. */
    struct evbuffer *input = bufferevent_get_input(bev);
    struct evbuffer *output = bufferevent_get_output(bev);

    static int nact;
    if (nact == 0) {
        printf("data from client: %lu\n", evbuffer_get_length(input));
        static proto_conn_resp41_t resp41;
        proto_pack_read(input, PROTO_CONN_RESP41, &resp41, sizeof(resp41));
        printf("resp41.capab_fs       : %X\n", resp41.capab_fs);
        printf("resp41.max_packet_size: %d\n", resp41.max_packet_size);
        printf("resp41.charset        : %d\n", resp41.charset);
        printf("resp41.username       : %s\n", resp41.username.data);
        printf("resp41.password.len   : %d\n", resp41.password.len);
        //printf("presp.password       : %s\n", dbg_hexprint(resp41.password.data, resp41.password.len));
        printf("resp41.schema         : %s\n", resp41.schema.data);
        printf("resp41.auth_plug_name : %s\n", resp41.auth_plug_name.data);
        //printf("resp41.attr.data      : %s\n", resp41.attr.data);

        static proto_resp_ok_t rok;
        rok.status_fl = 0x0002;
        proto_pack_write(output, PROTO_RESP_OK, &rok, sizeof(rok));
        nact++;
    }
    if (nact == 1) {
        printf("data from client: %lu\n", evbuffer_get_length(input));
        static proto_req_query_t rquery;
        proto_pack_read(input, PROTO_REQ_QUERY, &rquery, sizeof(rquery));
        printf("rquery.query.data : %s\n", rquery.query.data);
        if (rquery.query.len > 0) {
            if (mysql_query(mconn0, rquery.query.data)) {
                printf("can't execute query: %s\n", rquery.query.data);
            }
            if (mysql_query(mconn1, rquery.query.data)) {
                printf("can't execute query: %s\n", rquery.query.data);
            }

            MYSQL_RES *mresult0 = mysql_store_result(mconn0);
            MYSQL_RES *mresult1 = mysql_store_result(mconn1);

            static proto_resp_fcount_t rfcount;
            rfcount.fcount = mysql_num_fields(mresult0);
            proto_pack_write(output, PROTO_RESP_FCOUNT, &rfcount, sizeof(rfcount));

            MYSQL_FIELD *mfield;
            static proto_resp_field_t  rfield;
            while ((mfield = mysql_fetch_field(mresult0))) {
                printf("field name: %s\n", mfield->name);
                rfield.catalog.data = "def";
                rfield.catalog.len = strlen(rfield.catalog.data);
                rfield.name.data = mfield->name;
                rfield.name.len = strlen(rfield.name.data);
                rfield.charset = 33;
                rfield.length = 255;
                rfield.type = 253;
                rfield.decimals = 31;
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

            MYSQL_ROW mrow;
            while ((mrow = mysql_fetch_row(mresult0))) {
                int irow;
                for(irow = 0; irow < rfcount.fcount; irow++) {
                    rrow.values[irow].data = mrow[irow];
                    rrow.values[irow].len = strlen(rrow.values[irow].data);
                }
                proto_pack_write(output, PROTO_RESP_ROW, &rrow, sizeof(rrow));
            }
            while ((mrow = mysql_fetch_row(mresult1))) {
                int irow;
                for(irow = 0; irow < rfcount.fcount; irow++) {
                    rrow.values[irow].data = mrow[irow];
                    rrow.values[irow].len = strlen(rrow.values[irow].data);
                }
                proto_pack_write(output, PROTO_RESP_ROW, &rrow, sizeof(rrow));
            }
            proto_pack_write(output, PROTO_RESP_EOF, &reof, sizeof(reof));
        }
    }
}

static void
echo_event_cb(struct bufferevent *bev, short events, void *ctx)
{
    if (events & BEV_EVENT_ERROR)
        perror("Error from bufferevent");
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR)) {
        bufferevent_free(bev);
        printf("close connection\n");
    }
}

static void
accept_conn_cb(struct evconnlistener *listener,
    evutil_socket_t fd, struct sockaddr *address, int socklen,
    void *ctx)
{
    /* We got a new connection! Set up a bufferevent for it. */
    struct event_base *base = evconnlistener_get_base(listener);
    struct bufferevent *bev = bufferevent_socket_new(
           base, fd, BEV_OPT_CLOSE_ON_FREE);

    bufferevent_setcb(bev, echo_read_cb, NULL, echo_event_cb, NULL);

    bufferevent_enable(bev, EV_READ|EV_WRITE);

    printf("create connection\n");

    proto_conn_greet10_t pgreet;
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

    mconn0 = mysql_init(NULL);
    MYSQL *rconn0 =  mysql_real_connect(mconn0, "127.0.0.1", "root", "111111", "db0", 0, NULL, 0);
    if (rconn0 != NULL) {
        printf("connect to mysql db0");
    }

    mconn1 = mysql_init(NULL);
    MYSQL *rconn1 =  mysql_real_connect(mconn1, "127.0.0.1", "root", "111111", "db1", 0, NULL, 0);
    if (rconn1 != NULL) {
        printf("connect to mysql db1");
    }

}

static void
accept_error_cb(struct evconnlistener *listener, void *ctx)
{
    struct event_base *base = evconnlistener_get_base(listener);
    int err = EVUTIL_SOCKET_ERROR();
    fprintf(stderr, "Got an error %d (%s) on the listener. "
            "Shutting down.\n", err, evutil_socket_error_to_string(err));

    event_base_loopexit(base, NULL);
}

int
main(int argc, char **argv)
{
    struct event_base *base;
    struct evconnlistener *listener;
    struct sockaddr_in sin;

    int port = 9876;

    if (argc > 1) {
        port = atoi(argv[1]);
    }
    if (port<=0 || port>65535) {
        puts("Invalid port");
        return 1;
    }

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
    /* Listen on 0.0.0.0 */
    sin.sin_addr.s_addr = htonl(0);
    /* Listen on the given port. */
    sin.sin_port = htons(port);

    listener = evconnlistener_new_bind(base, accept_conn_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
        perror("Couldn't create listener");
        return 1;
    }
    evconnlistener_set_error_cb(listener, accept_error_cb);

    event_base_dispatch(base);
    return 0;
}
