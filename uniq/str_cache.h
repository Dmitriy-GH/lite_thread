// Кэш для строк

#pragma once
#include <stdint.h>
#include <assert.h>
#include <vector>

#define STR_BLOCK 0x100000 // Размер блока выделяемый одномоментно

class str_cache_t {
	std::vector<char*> mem;
	char* end;
	char* cur;
public:

	str_cache_t() {
		clear();
	}

	~str_cache_t() {
		clear();
	}

	// Освобождение памяти
	void clear() {
		for(size_t i = 0; i < mem.size(); i++) {
			delete[] mem[i];
		}
		mem.clear();
		end = NULL;
		cur = NULL;
	}

	// Добавление строки
	const char* add(const char* str, size_t len) {
		size_t empty = end - cur;
		if(empty < len + 1) {
			char* m = new char[STR_BLOCK];
			mem.push_back(m);
			cur = (char*)m;
			end = cur + STR_BLOCK;
			empty = STR_BLOCK;
		}
		if (empty > len) {
			memcpy(cur, str, len);
			cur[len] = 0;
			char* ret = cur;
			cur += len + 1;
			return ret;
		} else {
			return NULL;
		}
	}

	// Занято памяти
	size_t size() {
		return mem.size() * STR_BLOCK;
	}
};