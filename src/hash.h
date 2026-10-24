#ifndef SANSON_HASH_H
#define SANSON_HASH_H
#include <stddef.h>
void md5_hex(const void *data, size_t len, char out[33]);
void sha1_hex(const void *data, size_t len, char out[41]);
#endif
