#ifndef A0598F48_FF1D_49EC_AD52_0FF0E29863F9
#define A0598F48_FF1D_49EC_AD52_0FF0E29863F9

#ifdef __cplusplus
extern "C" {
#endif

#define PATH_MAX_CHAR_SIZE 64

#include <stdio.h>

int s_xfile_exists(const char *filename);
char *s_read_from_file(const char *name, const char * base);
int s_open(const char *name, const char * base, const char *mode);
int s_write(const char *name, const char * base, char *data, size_t len);
FILE *s_open_file(const char *name, const char * base, const char *mode);
int s_rename_file(const char *old, const char * n, const char * base);
int s_rename_file_n(const char *old, const char *n, uint8_t rmifexists);

#ifdef __cplusplus
}
#endif

#endif /* A0598F48_FF1D_49EC_AD52_0FF0E29863F9 */
