#ifndef AUX_H_
#define AUX_H_

#include <stddef.h>
#include <stdint.h>

#define AUX_LT_ERROR     (0x01)
#define AUX_LT_WARN      (0x02)
#define AUX_LT_INFO      (0x04)
#define AUX_LT_QUERY     (0x08)
#define AUX_LT_STAT      (0x10)

#define AUX_LT_NONE      (0x00)
#define AUX_LT_ALL       (0xFF)
#define AUX_LT_DEFAULT   (AUX_LT_ALL)

extern uint64_t aux_lt_mask;

char* aux_dbg_hexprint(char *data, size_t size);

void  aux_log(int lt, char *format, ...);

#endif /* AUX_H_ */
