#ifndef DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF
#define DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int fatfs_init();
int fatfs_uninit();
bool fatfs_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* DF86F9A9_7FFA_4B36_B839_8CF7E174D5CF */
