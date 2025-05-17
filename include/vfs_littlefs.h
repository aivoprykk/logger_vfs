#ifndef C3CB90E2_9EB1_46AE_A86F_45012A22E4EF
#define C3CB90E2_9EB1_46AE_A86F_45012A22E4EF

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
int littlefs_init();
int littlefs_mount();
void littlefs_unmount();
void littlefs_uninit();
bool littlefs_is_mounted();

#ifdef __cplusplus
}
#endif

#endif /* C3CB90E2_9EB1_46AE_A86F_45012A22E4EF */
