#pragma once
/* lite_thread_util.h

Вспомогательные классы для работы с lite_thread.h

*/

#include "lite_thread.h"
#include <map>

//----------------------------------------------------------------------------------
//------ ВОССТАНОВЛЕНИЕ ПОСЛЕДОВАТЕЛЬНОСТИ СООБЩЕНИЙ -------------------------------
//----------------------------------------------------------------------------------
/* Актор, восстанавливающий порядок прохождения сообщений, утерянный из-за
   распараллеливания в процессе обработки.

   Перед началом работы принимает сообщение с хэндлом актора, которому отправлять
   упорядоченную последовательность.

   Принимает на вход сообщения типа Т, пришедшие не по порядку кэширует.

   тип сообщения T должен иметь поле size_t idx 
   при отправке сообщений нумеровать idx с нуля.

*/

template <typename T>
class lite_order_t : public lite_actor_t {
	typedef std::map<size_t, lite_msg_t*> cache_t;

	cache_t cache;			// Кэш для пришедших в неправильном порядке
	size_t next;			// Номер следующего на отправку
	lite_actor_t* send_to;	// Адрес отправки упорядоченной последовательности

public:
	lite_order_t(const std::string next_actor) : next(0) {
		send_to = lite_actor_get(next_actor);
		if (send_to == NULL) { // Не задан адрес пересылки
			lite_log(LITE_ERROR_USER, "Can`t find actor '%s'", next_actor.c_str());
			assert(send_to == NULL);
			return;
		}
		// Типы принимаемых сообщений
		type_add(lite_msg_type<T>());
	}

	~lite_order_t() {
		if(cache.size() != 0) {
			lite_log(LITE_ERROR_USER, "%s have %d msg in cache", name_get().c_str(), cache.size());
			for(auto& it : cache) {
				delete it.second; // Явное удаление, т.к. было копирование
			}
			cache.clear();
		}
	}

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		T* m = static_cast<T*>(msg);

		if(m->idx == next) {
			// Сообщение пришло по порядку
			send_to->run(msg);
			next++;
			// Поиск в кэше и отправка 
			cache_t::iterator it = cache.find(next);
			while(it != cache.end() && it->first == next) {
				send_to->run(it->second);
				cache_t::iterator it_del = it;
				it++;
				next++;
				cache.erase(it_del);
			}
		} else {
			// Пришло не по порядку, сохранение в кэш
			cache_t::iterator it = cache.find(m->idx);
			if(it != cache.end()) { // Сообщение с таким номером уже есть в кэше
				lite_log(LITE_ERROR_USER, "%s receive two msg with idx#%llu", name_get().c_str(), (uint64_t)m->idx);
				return;
			}
			cache[m->idx] = lite_msg_copy(m);
		}
	}
};

//----------------------------------------------------------------------------------
//------ КОЛИЧЕСТВО ЯДЕР ПРОЦЕССОРА ------------------------------------------------
//----------------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
int lite_processor_count() {
	SYSTEM_INFO sysinfo;
	GetSystemInfo(&sysinfo);
	return (int)(sysinfo.dwNumberOfProcessors > 0 ? sysinfo.dwNumberOfProcessors : 1);
}
#else
#define <unistd.h>
#define <limits.h>

int lite_processor_count() {
	int cnt = (int)sysconf(_SC_NPROC_ONLN);
	return cnt > 0 ? cnt : 1;
}
#endif

//----------------------------------------------------------------------------------
//------ СЧЕТЧИК С ОЖИДАНИЕМ -------------------------------------------------------
//----------------------------------------------------------------------------------
// Счетчик для управления размером очереди. При достижении max ожидает time_ms уменьшения до min
class lite_wait_counter_t : public lite_align64_t {
	int min;
	int max;
	int time_ms;
	std::atomic<int> size;
	std::mutex mtx;				// Для засыпания
	std::condition_variable cv;	// Для засыпания
public:
	lite_wait_counter_t(int time_wait_ms, int max_size, int min_size = 0) {
		size = 0;
		time_ms = time_wait_ms;
		max = max_size;
		min = (min_size <= 0) ? max * 6 / 10 + 1 : min_size;
	}

	// Увеличение счетчика. true - уложился в отведенное время
	bool inc() noexcept {
		bool ret = true;
		if (++size >= max) {
			std::unique_lock<std::mutex> lck(mtx);
			if (size >= max) {
				ret = cv.wait_for(lck, std::chrono::milliseconds(time_ms)) != std::cv_status::timeout;
			}
		}
		return ret;
	}

	// Уменьшение счетчика
	void dec() noexcept {
		if (size-- >= min) {
			if (size < min) {
				std::unique_lock<std::mutex> lck(mtx);
				if (size < min) cv.notify_all();
			}
		}
	}

	// Текущее состояние
	int count() noexcept {
		return size;
	}
};
