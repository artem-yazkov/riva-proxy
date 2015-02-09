#ifndef SESSION_H_
#define SESSION_H_

#include <mysql/mysql.h>
#include "proxy.h"

#define SESSION_PHASE_CONN    0
#define SESSION_PHASE_QUERY   1

typedef struct session {
    MYSQL       **dbc;
    MYSQL_RES   **mres;
    int           db_cnt;
    int           phase;
    proxy_cfg_t  *cfg;
} session_t;

void
session_accept_conn(
        struct evconnlistener *listener,
        evutil_socket_t fd,
        struct sockaddr *address,
        int socklen,
        void *ctx);

void
session_accept_error(struct evconnlistener *listener, void *ctx);


#endif /* SESSION_H_ */
