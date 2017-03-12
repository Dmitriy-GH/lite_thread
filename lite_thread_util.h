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
class lite_order_t : public lite_worker_t {
	typedef std::map<size_t, lite_msg_t*> cache_t;

	cache_t cache;			// Кэш для пришедших в неправильном порядке
	size_t next;			// Номер следующего на отправку
	lite_actor_t* send_to;	// Адрес отправки упорядоченной последовательности

public:
	lite_order_t() : next(0), send_to(NULL) {
		// Типы принимаемых сообщений
		type_add(lite_msg_type<lite_actor_t*>());
		type_add(lite_msg_type<T>());
	}

	~lite_order_t() {
		if(cache.size() != 0) {
			lite_error("%s have %d msg in cache", handle()->name_get().c_str(), cache.size());
			for(auto& it : cache) {
				lite_msg_erase(it.second); // Явное удаление, т.к. было копирование
			}
			cache.clear();
		}
	}

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		T* d = lite_msg_data<T>(msg);
		if(d == NULL) {
			// Получен адрес куда отправлять
			lite_actor_t** a = lite_msg_data<lite_actor_t*>(msg);
			assert(a != NULL);
			send_to = *a;
			return;
		} else if(send_to == NULL) { // Не задан адрес пересылки
			lite_error("Can`t set receiver for %s", handle()->name_get().c_str());
			return;
		}

		if(d->idx == next) {
			// Сообщение пришло по порядку
			lite_thread_run(msg, send_to);
			next++;
			// Поиск в кэше и отправка 
			cache_t::iterator it = cache.find(next);
			while(it != cache.end() && it->first == next) {
				lite_thread_run(it->second, send_to);
				cache_t::iterator it_del = it;
				it++;
				next++;
				cache.erase(it_del);
			}
		} else {
			// Пришло не по порядку, сохранение в кэш
			cache_t::iterator it = cache.find(d->idx);
			if(it != cache.end()) { // Сообщение с таким номером уже есть в кэше
				lite_error("%s receive two msg with idx#%llu", handle()->name_get().c_str(), (uint64_t)d->idx);
				return;
			}
			cache[d->idx] = lite_msg_copy(msg);
		}
	}
};

// Создание объекта и инициализация
template <typename T>
static lite_actor_t* lite_order_create(const std::string& name, lite_actor_t* send_to) {
	lite_actor_t* la = lite_actor_create<lite_order_t<T>>(name);
	lite_msg_t* msg = lite_msg_create<lite_actor_t*>();
	lite_actor_t** a = lite_msg_data<lite_actor_t*>(msg);
	assert(a != NULL);
	*a = send_to;
	lite_thread_run(msg, la); // Отправка хэндла куда слать упорядоченный поток сообщений
	return la;
}