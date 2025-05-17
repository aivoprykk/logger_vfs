#ifndef DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF
#define DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

int fatfs_init();
int fatfs_mount();
void fatfs_umount();
void fatfs_uninit();
bool fatfs_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF */
