// Хэш-фуникция

#pragma once
#include <stdint.h>

typedef uint32_t hash_t;

hash_t hash32(const void* data, size_t len, hash_t prev = 0) {
	const char* str = (const char*) data;
	hash_t h = prev;
	while (len--) h = (h << 5) + h + *str++;
	return h;
}

hash_t hash32(const char* str) {
	hash_t h = 0;
	while (*str) h = (h << 5) + h + *str++;
	return h;
}