#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include "aux.h"
#include "config.h"

typedef struct __storage __storage_t;
struct __storage {
    char        *addr;
    char        *addr_url;
    MYSQL       *dbdata;
    __storage_t *next;
};
static __storage_t *__storages;

typedef struct __table __table_t;
struct __table {
    char         *name;
    __storage_t **storages;
    uint32_t      storage_cnt;
    void        **storage_uptrs;
    __table_t    *next;
};
static __table_t *__tables;

typedef struct __config {
    char *host;
    int   port;
    char *user;
    char *pass;
    char *dbname;

    char *url;
    MYSQL *dbcfg;
} __config_t;
static __config_t *__config;

struct config_tbl_hdl {
    __table_t *table;
    uint32_t   istorage;
};
const char *CONFIG_DB_NAME = "data";
const char *QUERY_GET_TBL_STORAGE =
        "SELECT d.type, d.name, s.path AS addr, s.mode "
        "FROM map_data2storage AS d "
        "JOIN sys_storages AS s ON d.storg_distr=s.id "
        "WHERE s.type = 'mysql' AND (s.mode = 'r-' OR s.mode = 'rw') "
        "ORDER BY d.name";

static int __add_tbl_storage(char *tbl, char *addr)
{
    __storage_t *istor, *istor_prev;
    __table_t   *itbl, *itbl_prev;

    /* storage search/insert */
    for (istor = __storages, istor_prev = NULL;
         (istor != NULL) && (strcmp(istor->addr, addr) != 0);
         istor_prev = istor, istor = istor->next);
    if (istor == NULL) {
        istor = calloc(1, sizeof(__storage_t));
        istor->addr = strdup(addr);
        if (istor_prev == NULL) {
            __storages = istor;
        } else {
            istor_prev->next = istor;
        }
    }

    /* table search/insert */
    for (itbl = __tables, itbl_prev = NULL;
         (itbl != NULL) && (strcmp(itbl->name, tbl) != 0);
         itbl_prev = itbl, itbl = itbl->next);
    if (itbl == NULL) {
        itbl = calloc(1, sizeof(__table_t));
        itbl->name = strdup(tbl);
        if (itbl_prev == NULL) {
            __tables = itbl;
        } else {
            itbl_prev->next = itbl;
        }
    }

    /* table->storage link search/update */
    for (int is = 0; is < itbl->storage_cnt; is++) {
        if (itbl->storages[is] == istor) {
            return 0;
        }
    }
    itbl->storages = realloc(itbl->storages, itbl->storage_cnt + 1);
    itbl->storages[itbl->storage_cnt] = istor;
    itbl->storage_uptrs = realloc(itbl->storage_uptrs, itbl->storage_cnt + 1);
    itbl->storage_uptrs[itbl->storage_cnt] = NULL;
    itbl->storage_cnt++;

    return 0;
}

int config_init(char *host, int port, char *user, char *pass, char *dbname)
{
    /* Initialize __config structure */
    __config = calloc(1, sizeof(__config_t));

    __config->host = strdup(host);
    __config->port = (port <= 0) ? 3306 : port;
    __config->user = strdup(user);
    __config->pass = strdup(pass);
    __config->dbname = strdup(dbname);
    static char url[PATH_MAX];
    snprintf(url, sizeof(url)-1, "%s:%s@%s:%d/%s",
            __config->user,
            __config->pass,
            __config->host,
            __config->port,
            __config->dbname);
    __config->url = strdup(url);

    /* Retrieve info about table storages */
    __config->dbcfg = mysql_init(NULL);
    void *cres = mysql_real_connect(
            __config->dbcfg,
            __config->host,
            __config->user,
            __config->pass,
            __config->dbname,
            __config->port,
            NULL, 0);
    if (cres == NULL) {
        aux_log(AUX_LT_ERROR, "Can't connect to <%s> : %s", __config->url, mysql_error(__config->dbcfg));
        return -1;
    }
    __config->dbcfg->reconnect = 1;

    int qresult = mysql_query(__config->dbcfg, QUERY_GET_TBL_STORAGE);
    if (mysql_errno(__config->dbcfg) > 0) {
        aux_log(AUX_LT_ERROR, "[%s]: %s (query: %s)",
                __config->url, (char *)mysql_error(__config->dbcfg), QUERY_GET_TBL_STORAGE);
        return -1;
    }
    if (qresult != 0) {
        return -1;
    }

    MYSQL_RES *mresult;
    mresult = mysql_store_result(__config->dbcfg);

    /* Fill __storages, __tables lists */
    char     *tbl_name = NULL;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(mresult))) {
        if ((tbl_name == NULL) || (strcmp(tbl_name, row[1]) != 0)) {
            __add_tbl_storage(row[1], row[2]);
            tbl_name = row[1];
        }
    }
    mysql_free_result(mresult);

    /* Connect to the storages */
    for (__storage_t *istor = __storages; istor != NULL; istor = istor->next) {
        static char url[PATH_MAX];
        snprintf(url, sizeof(url)-1, "%s:%s@%s:%d/%s",
                __config->user,
                __config->pass,
                istor->addr,
                __config->port,
                CONFIG_DB_NAME);
        istor->addr_url = strdup(url);

        istor->dbdata = mysql_init(NULL);
        void *cres = mysql_real_connect(
                istor->dbdata,
                istor->addr,
                __config->user,
                __config->pass,
                CONFIG_DB_NAME,
                __config->port,
                NULL, 0);
        if (cres == NULL) {
            aux_log(AUX_LT_ERROR, "Can't connect to: <%s>: %s", istor->addr_url, mysql_error(istor->dbdata));
            return -1;
        }
        istor->dbdata->reconnect = 1;
    }

    return 0;
}

bool config_tbl_search(char *name, config_tbl_hdl_t **hdl)
{
    *hdl = calloc(1, sizeof(config_tbl_hdl_t));
    for ((*hdl)->table = __tables;
        ((*hdl)->table != NULL) && (strcmp((*hdl)->table->name, name) != 0);
         (*hdl)->table = (*hdl)->table->next);
    if ((*hdl)->table == NULL) {
        free(*hdl);
        return false;
    }
    return true;
}

uint32_t config_tbl_st_first(config_tbl_hdl_t *hdl)
{
    hdl->istorage = 0;
    return hdl->table->storage_cnt;
}

bool config_tbl_st_next(config_tbl_hdl_t *hdl, MYSQL **dbc, void **uptr)
{
    if (hdl->istorage >= hdl->table->storage_cnt) {
        return false;
    }
    *dbc = hdl->table->storages[hdl->istorage]->dbdata;
    *uptr = hdl->table->storage_uptrs[hdl->istorage];
    hdl->istorage++;

    return true;
}

void config_tbl_st_set_uptr(config_tbl_hdl_t *hdl, void *uptr)
{
    hdl->table->storage_uptrs[hdl->istorage-1] = uptr;
}
