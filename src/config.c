#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>

#include "aux.h"
#include "config.h"

typedef struct storage storage_t;
struct storage {
    char        *addr;
    char        *addr_url;
    MYSQL       *dbdata;
    storage_t   *next;
};
static storage_t *storages;

typedef struct table table_t;
struct table {
    char        *name;
    storage_t  **storages;
    uint32_t     storage_cnt;
    void       **storage_uptrs;
    table_t     *next;
};
static table_t *tables;

typedef struct config {
    char *host;
    int   port;
    char *user;
    char *pass;
    char *dbname;

    char *url;
    MYSQL *dbcfg;
} config_t;
static config_t *config;

struct config_tbl_hdl {
    table_t   *table;
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
    storage_t *istor, *istor_prev;
    table_t   *itbl, *itbl_prev;

    /* storage search/insert */
    for (istor = storages, istor_prev = NULL;
         (istor != NULL) && (strcmp(istor->addr, addr) != 0);
         istor_prev = istor, istor = istor->next);
    if (istor == NULL) {
        istor = calloc(1, sizeof(storage_t));
        istor->addr = strdup(addr);
        if (istor_prev == NULL) {
            storages = istor;
        } else {
            istor_prev->next = istor;
        }
    }

    /* table search/insert */
    for (itbl = tables, itbl_prev = NULL;
         (itbl != NULL) && (strcmp(itbl->name, tbl) != 0);
         itbl_prev = itbl, itbl = itbl->next);
    if (itbl == NULL) {
        itbl = calloc(1, sizeof(table_t));
        itbl->name = strdup(tbl);
        if (itbl_prev == NULL) {
            tables = itbl;
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
    /* Initialize config structure */
    config = calloc(1, sizeof(config_t));

    config->host = strdup(host);
    config->port = (port <= 0) ? 3306 : port;
    config->user = strdup(user);
    config->pass = strdup(pass);
    config->dbname = strdup(dbname);
    static char url[PATH_MAX];
    snprintf(url, sizeof(url)-1, "%s:%s@%s:%d/%s",
            config->user,
            config->pass,
            config->host,
            config->port,
            config->dbname);
    config->url = strdup(url);

    /* Retrieve info about table storages */
    config->dbcfg = mysql_init(NULL);
    void *cres = mysql_real_connect(
            config->dbcfg,
            config->host,
            config->user,
            config->pass,
            config->dbname,
            config->port,
            NULL, 0);
    if (cres == NULL) {
        aux_log(AUX_LT_ERROR, "Can't connect to <%s> : %s", config->url, mysql_error(config->dbcfg));
        return -1;
    }
    config->dbcfg->reconnect = 1;

    int qresult = mysql_query(config->dbcfg, QUERY_GET_TBL_STORAGE);
    if (mysql_errno(config->dbcfg) > 0) {
        aux_log(AUX_LT_ERROR, "[%s]: %s (query: %s)",
                config->url, (char *)mysql_error(config->dbcfg), QUERY_GET_TBL_STORAGE);
        return -1;
    }
    if (qresult != 0) {
        return -1;
    }

    MYSQL_RES *mresult;
    mresult = mysql_store_result(config->dbcfg);

    /* Fill storages, tables lists */
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
    for (storage_t *istor = storages; istor != NULL; istor = istor->next) {
        static char url[PATH_MAX];
        snprintf(url, sizeof(url)-1, "%s:%s@%s:%d/%s",
                config->user,
                config->pass,
                istor->addr,
                config->port,
                CONFIG_DB_NAME);
        istor->addr_url = strdup(url);

        istor->dbdata = mysql_init(NULL);
        void *cres = mysql_real_connect(
                istor->dbdata,
                istor->addr,
                config->user,
                config->pass,
                CONFIG_DB_NAME,
                config->port,
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
    for ((*hdl)->table = tables;
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
    if (dbc != NULL) {
        *dbc = hdl->table->storages[hdl->istorage]->dbdata;
    }
    if (uptr != NULL) {
        *uptr = hdl->table->storage_uptrs[hdl->istorage];
    }
    hdl->istorage++;

    return true;
}

void config_tbl_st_set_uptr(config_tbl_hdl_t *hdl, void *uptr)
{
    hdl->table->storage_uptrs[hdl->istorage-1] = uptr;
}
