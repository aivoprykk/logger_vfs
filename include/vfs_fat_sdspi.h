#ifndef C7B9F109_1993_459D_836F_6C0747D836CD
#define C7B9F109_1993_459D_836F_6C0747D836CD

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int sdcard_init(void);
void sdcard_uninit(void);
uint32_t sdcard_space();

#ifdef __cplusplus
}
#endif
#endif /* C7B9F109_1993_459D_836F_6C0747D836CD */
