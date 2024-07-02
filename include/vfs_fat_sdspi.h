#ifndef C7B9F109_1993_459D_836F_6C0747D836CD
#define C7B9F109_1993_459D_836F_6C0747D836CD

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int sdcard_init(void);
void sdcard_uninit(void);
uint32_t sdcard_space(void);
int sdcard_mount(void);
void sdcard_umount(void);
bool sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* C7B9F109_1993_459D_836F_6C0747D836CD */
