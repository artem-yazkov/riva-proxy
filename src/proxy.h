#ifndef PROXY_H_
#define PROXY_H_

#define PROXY_CFG_DB_HOST     "dbconfig"
#define PROXY_CFG_DB_PORT     3306
#define PROXY_CFG_DB_USER     "rivasense"
#define PROXY_CFG_DB_PASS     "rivapass"
#define PROXY_CFG_DB_NAME     "config"
#define PROXY_CFG_DB_URL      "rivasense:rivapass@dbconfig:3306/config"

#define PROXY_CFG_LISTEN_HOST "0.0.0.0"
#define PROXY_CFG_LISTEN_PORT 9876
#define PROXY_CFG_NAME_MAXLEN 64

typedef struct proxy_cfg_db {
    char *url;
    char database[PROXY_CFG_NAME_MAXLEN];
    char username[PROXY_CFG_NAME_MAXLEN];
    char password[PROXY_CFG_NAME_MAXLEN];
    char host    [PROXY_CFG_NAME_MAXLEN];
    int  port;
} proxy_cfg_db_t;

typedef struct proxy_cfg {
    char            lhost   [PROXY_CFG_NAME_MAXLEN];
    int             lport;
    proxy_cfg_db_t  dbcfg;
    int             logtypes;
    int             limit;
} proxy_cfg_t;

#endif /* PROXY_H_ */
