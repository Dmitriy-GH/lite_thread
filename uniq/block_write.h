#pragma once
#include <stdio.h>
#include <stdint.h>
#include <assert.h>

// Чтение текстового файла поблочно
class block_write_t {
	FILE* file;
	char* buf = NULL;
	char* buf_end = NULL;
	char* cur = NULL;

public:
	void init(size_t size, FILE* f) {
		buf = (char*)malloc(size);
		assert(buf != NULL);
		buf_end = buf + size;
		cur = buf;
		file = f;
	}

	~block_write_t() {
		if (buf != NULL) {
			flush();
			free(buf);
		}
	}

	// Запись буфера в файл
	void flush() {
		if (cur != buf) {
			fwrite(buf, 1, cur - buf, file);
		}
		cur = buf;
	}

	// Запись строки
	void write_str(const char* str, size_t len) {
		size_t empty = buf_end - cur;
		if(empty <= len) {
			flush();
			empty = buf_end - cur;
		}
		if (empty <= len) {
			assert(empty <= len);
		} else {
			memcpy(cur, str, len);
			cur += len;
			*cur = '\n';
			cur++;
		}
	}
};
                                                                                               