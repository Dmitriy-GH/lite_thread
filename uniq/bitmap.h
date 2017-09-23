// Биткарта

#pragma once
#include <stdint.h>
#include <assert.h>

class bitmap_t {
	uint32_t* bitmap = NULL;
	size_t len = 0; // Общий размер выделенной памяти
public:
	bitmap_t() {
	}

	bitmap_t(size_t size) {
		init(size);
	}

	~bitmap_t() {
		clear();
	}

	// Выделение памяти и инициализация нулями
	bool init(size_t size) {
		clear();
		len = size >> 5;
		if ((size & 31) != 0) len++;
		bitmap = (uint32_t*) calloc(len, sizeof(uint32_t));
		assert(bitmap != NULL);
		if (bitmap == NULL) len = 0;
		return bitmap != NULL;
	}

	// Освобождение памяти
	void clear() {
		if (bitmap != NULL) {
			free(bitmap);
			bitmap = NULL;
		}
		len = 0;
	}

	// Установка бита
	void set(size_t pos) {
		size_t idx = pos >> 5;
		assert(idx <= len);
		bitmap[idx] |= 1 << (pos & 31);
	}

	// Сброс бита
	void reset(size_t pos) {
		size_t idx = pos >> 5;
		assert(idx <= len);
		bitmap[idx] &= ~(1 << (pos & 31));
	}

	// Получение бита
	bool get(size_t pos) {
		size_t idx = pos >> 5;
		assert(idx <= len);
		return (bitmap[idx] & (1 << (pos & 31))) != 0;
	}

	// Установка и получение предыдущего значения бита
	bool getset(size_t pos) {
		size_t idx = pos >> 5;
		assert(idx <= len);
		uint32_t mask = 1 << (pos & 31);
		bool ret = (bitmap[idx] & mask) != 0;
		bitmap[idx] |= mask;
		return ret;
	}

};