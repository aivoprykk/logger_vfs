#ifndef C7B9F109_1993_459D_836F_6C0747D836CD
#define C7B9F109_1993_459D_836F_6C0747D836CD

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

int sdcard_init(void);
int sdcard_mount(void);
void sdcard_umount(void);
void sdcard_uninit(void);
bool sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* C7B9F109_1993_459D_836F_6C0747D836CD */
