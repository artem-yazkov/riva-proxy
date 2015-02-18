#ifndef CONFIG_H_
#define CONFIG_H_

#include <stdbool.h>
#include <stdint.h>

#include <mysql/mysql.h>

int config_init(char *host, int port, char *user, char *pass, char *dbname);

typedef struct config_tbl_hdl config_tbl_hdl_t;

bool     config_tbl_search(char *name, config_tbl_hdl_t **hdl);
uint32_t config_tbl_st_first(config_tbl_hdl_t *hdl);
bool     config_tbl_st_next(config_tbl_hdl_t *hdl, MYSQL **dbc, void **uptr);
void     config_tbl_st_set_uptr(config_tbl_hdl_t *hdl, void *uptr);

#endif /* CONFIG_H_ */
