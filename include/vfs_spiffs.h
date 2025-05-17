#ifndef D089C21E_9304_4B3D_99BE_D26B2335AE26
#define D089C21E_9304_4B3D_99BE_D26B2335AE26

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

int spiffs_init(void);
int spiffs_mount(void);
void spiffs_umount(void);
void spiffs_uninit();
bool spiffs_is_mounted(void);

#ifdef __cplusplus
}
#endif
#endif /* D089C21E_9304_4B3D_99BE_D26B2335AE26 */
