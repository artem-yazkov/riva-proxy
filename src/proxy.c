#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <arpa/inet.h>
#include <event2/listener.h>

#include "aux.h"
#include "config.h"
#include "protocol.h"
#include "proxy.h"
#include "session.h"

static proxy_cfg_t proxy_cfg;

static void
help(char *errstr)
{
    if (errstr != NULL) {
        printf("%s\n", errstr);
    }
}

static void
process_args(int argc, char * const argv[])
{
    strncpy(proxy_cfg.lhost, PROXY_CFG_LISTEN_HOST, sizeof(proxy_cfg.lhost) - 1);
    proxy_cfg.lport = PROXY_CFG_LISTEN_PORT;
    aux_lt_mask = proxy_cfg.logtypes = AUX_LT_DEFAULT;

    static struct option options[] = {
        /* name, has_arg, flag, value */
        { .name = "help"         , no_argument       , NULL , 'h' },
        { .name = "version"      , no_argument       , NULL , 'v' },
        { .name = "logtypes"     , required_argument , NULL , 'l' },
        { .name = "limit"        , required_argument , NULL , 'L' },
        { .name = "listen"       , required_argument , NULL , 'a' },
        { .name = "config"       , required_argument , NULL , 'c' },
        { NULL                   , 0                 , NULL ,  0  }
    };
    int opt;

    while ((opt = getopt_long_only(argc, argv, "hvl:L:a:c:", options, NULL)) != -1) {
        /* Listen address */
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
            help("Incorrect proxy-address");
            exit(1);
        }
        /* Connection to the config DB */
        if (opt == 'c') {
            proxy_cfg_db_t *db = &proxy_cfg.dbcfg;
            memset(db, 0, sizeof(proxy_cfg_db_t));
            db->url = strdup(optarg);

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
            if (args == 4) {
                continue;
            }
            /* default password, port */
            args = sscanf(optarg, "%[^@]@%[^/]/%s", db->username, db->host, db->database);
            if (args == 3) {
                continue;
            }
            /* incorrect pattern */
            help("Incorrect backend");
            exit(1);
        }
        /* Help */
        if (opt == 'h') {
            help(NULL);
        }
        /* Logtypes */
        if (opt == 'l') {
            proxy_cfg.logtypes = AUX_LT_NONE;
            char *tok = strtok(optarg, ",");
            while (tok != NULL) {
                if (!strcasecmp(tok, "error")) {
                    proxy_cfg.logtypes |= AUX_LT_ERROR;
                } else if (!strcasecmp(tok, "warn")) {
                    proxy_cfg.logtypes |= AUX_LT_WARN;
                } else if (!strcasecmp(tok, "info")) {
                    proxy_cfg.logtypes |= AUX_LT_INFO;
                } else if (!strcasecmp(tok, "query")) {
                    proxy_cfg.logtypes |= AUX_LT_QUERY;
                } else if (!strcasecmp(tok, "stat")) {
                    proxy_cfg.logtypes |= AUX_LT_STAT;
                } else if (!strcasecmp(tok, "none")) {
                    proxy_cfg.logtypes |= AUX_LT_NONE;
                } else if (!strcasecmp(tok, "all")) {
                    proxy_cfg.logtypes |= AUX_LT_ALL;
                } else {
                    help("Incorrect logtypes");
                }
                tok = strtok(NULL, ",");
            }
            aux_lt_mask = proxy_cfg.logtypes;
        }
        /* Output limit */
        if (opt == 'L') {
            proxy_cfg.limit = atoi(optarg);
        }
    }

    if (strlen(proxy_cfg.dbcfg.host) == 0) {
        help("'--config' argument must be specified");
        exit(1);
    }
}

int
main(int argc, char **argv)
{
    process_args(argc, argv);
    int cresult = config_init(
            proxy_cfg.dbcfg.host,
            proxy_cfg.dbcfg.port,
            proxy_cfg.dbcfg.username,
            proxy_cfg.dbcfg.password,
            proxy_cfg.dbcfg.database);
    if (cresult < 0) {
        return cresult;
    }

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

    listener = evconnlistener_new_bind(base, session_accept_conn, &proxy_cfg,
        LEV_OPT_CLOSE_ON_FREE|LEV_OPT_REUSEABLE, -1,
        (struct sockaddr*)&sin, sizeof(sin));
    if (!listener) {
        perror("Couldn't create listener");
        return 1;
    }
    evconnlistener_set_error_cb(listener, session_accept_error);

    event_base_dispatch(base);
    return 0;
}
