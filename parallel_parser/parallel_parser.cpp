/* Параллельная обработка потока MSG_COUNT сообщений WORKER_COUNT обработчиками.
*/

#ifndef _DEBUG
#define WORKER_COUNT 3  // Количество обработчиков
#define MSG_COUNT	1000000	// Количество сообщений
#else
#define WORKER_COUNT 3  // Количество обработчиков
#define MSG_COUNT	100	// Количество сообщений
#endif

#define CPU_MAX 8 // Максимальное количество одновременно работающих потоков
//---------------------------------------------------------------------
//#define LT_DEBUG
#define LT_STAT
//#define LT_STAT_QUEUE
//#define LT_DEBUG_LOG
//#define LT_XP_DLL
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../lite_thread.h"
#include <atomic>
#include <vector>
#include <stdio.h>
#include <string.h>

//---------------------------------------------------------------------
// Содержимое сообщения
struct msg_t : public lite_msg_t {
	int number;	// Номер сообщения
};

//---------------------------------------------------------------------
class worker_t : public lite_actor_t {
	lite_actor_t* next; // Следующий обработчик
	int64_t sum = 0; // Контрольная сумма

	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);

		// Обработка сообщения
		sum += m->number;

		// Передеча дальше
		next->run(m);
	}

public:
	// Конструктор
	worker_t(lite_actor_t* next) : next(next) {	
		type_add(lite_msg_type<msg_t>());
	}

	// Завершение работы актора
	~worker_t() {
		lite_log(0, "sum %lld", sum);
	}

};

//---------------------------------------------------------------------
// Подготовка сообщения и отправка на обработку
class start_t : public lite_actor_t {
	int count = {MSG_COUNT}; // Счетчик исходящих сообщений
	lite_actor_t* next; // Следующий обработчик

	void recv(lite_msg_t* msg) override {
		if (count <= 0) return;
			
		msg_t* m = static_cast<msg_t*>(msg);
		
		// Создание сообщения
		m->number = --count;

		next->run(m);
	}
public:
	// Конструктор
	start_t() {
		type_add(lite_msg_type<msg_t>());
	}

	// Следующий обработчик
	void next_set(lite_actor_t* next) {
		this->next = next;
	}
};

//---------------------------------------------------------------------
// Проверка заполнения сообщения
class finish_t : public lite_actor_t {
	lite_actor_t* next; // Следующий обработчик
	int64_t start; // Время запуска

	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);

		if(m->number == 0) { // Последнее сообщение
			uint32_t time = (uint32_t)(lite_time_now() - start);
			lite_log(0, "time %d msec  speed %d msg/sec\n", time, MSG_COUNT * 1000 / (time == 0 ? 1 : time));
		} else {
			next->run(m);
		}
	}
public:
	// Конструктор
	finish_t(lite_actor_t* next) : next(next) {	
		start = lite_time_now();
		type_add(lite_msg_type<msg_t>());
	}
};


int main()
{
	lite_log(0, "compile %s %s", __DATE__, __TIME__);
	lite_log(0, "START workers: %d  messages: %d", WORKER_COUNT, MSG_COUNT);

	// Установка ограничения количества потоков
	lite_thread_max(CPU_MAX);

	// Инициализация акторов
	start_t* start = new start_t;
	start->name_set("start");

	lite_actor_t* next = start;

	for(size_t i = 0; i!= WORKER_COUNT; i++) {
		next = new worker_t(next);
		next->name_set(std::string("worker#") + std::to_string(i));
	}

	finish_t* finish = new finish_t(next);
	finish->name_set("finish");

	start->next_set(finish);

	// Создание сообщений
	for(size_t i = 0; i != 100; i++) {
		msg_t* msg = new msg_t();
		start->run(msg);
	}
	
	lite_thread_end(); // Ожидание окончания расчета

	printf("compile %s %s with %s\n", __DATE__, __TIME__, LOCK_TYPE_LT);

#ifdef _DEBUG
	printf("Press any key ...");
	getchar();
#endif
	return 0;
}
