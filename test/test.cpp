// Простейшие примеры использования с выводом всего происходящего в потоках в консоль
#define LT_DEBUG
#define LT_DEBUG_LOG
#include "../lite_thread.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

//------------------------------------------------------------------------
// Сообщение
class msg_t : public lite_msg_t {
public:
	uint32_t x;
};

//------------------------------------------------------------------------
// Актор (обработчик сообщения)
class actor_t : public lite_actor_t {
	void recv(lite_msg_t* msg) override {
		msg_t* m = dynamic_cast<msg_t*>(msg);
		assert(m != NULL);
		lite_log(0, "thread#%d recv %d", (int)lite_thread_num(), m->x); // Начало обработки
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		lite_log(0, "thread#%d end %d", (int)lite_thread_num(), m->x); // Конец
	}
};

//------------------------------------------------------------------------
// Тест 1.
// 10 запусков подряд (в выводе recv 100+)

void test1() { // Основной поток
	lite_log(0, "--- test 1 ---");
	actor_t* la = new actor_t();
	assert(la != NULL);
	// Отправка 10 сообщений
	for (int i = 100; i < 110; i++) {
		msg_t* msg = new msg_t; // Создание сообщения
		assert(msg != NULL);
		msg->x = i;
		la->run(msg); // Постановка в очередь на выполнение
	}
	// Ожидание завершения работы
	lite_thread_end();
}

//------------------------------------------------------------------------
// Тест 2.
// 20 запусков подряд с обработкой в 3 потока (в выводе recv 200+)

void test2() { // Основной поток
	lite_log(0, "--- test 2 ---");
	actor_t* la = new actor_t();
	la->parallel_set(3); // Глубина распараллеливания 3 потока
	for (int i = 200; i < 220; i++) {
		msg_t* msg = new msg_t; // Создание сообщения
		assert(msg != NULL);
		msg->x = i;
		la->run(msg); // Постановка в очередь на выполнение
	}
	// Ожидание завершения работы
	lite_thread_end();
}


//------------------------------------------------------------------------
// Тест 3.
// 1 запуск и 10 рекурсивных вызовов (в выводе recv 300+)

class recurse_t : public lite_actor_t {
	int count; // Количество вызовов

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = dynamic_cast<msg_t*>(msg);
		assert(m != NULL);
		lite_log(0, "thread#%d recv %d", (int)lite_thread_num(), m->x); // Начало обработки
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		lite_log(0, "thread#%d end %d", (int)lite_thread_num(), m->x); // Конец

		m->x++; // Изменение сообщения
		count++; // Счетчик отправок

		if (count < 10) {
			run(msg); // Отправляем сообщение себе
		}
	}

public:
	// Конструктор
	recurse_t() {
		count = 0;
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	~recurse_t() {
		lite_log(0, "thread#%d worker end. count = %d", (int)lite_thread_num(), count);
	}
};

// Сообщение необрабатываемого типа
class msg_bad_t : public lite_msg_t {
public:
	uint32_t x;
};


void test3() { // Основной поток
	lite_log(0, "--- test 3 ---");
	recurse_t* la = new recurse_t();
	la->name_set("recurse_t");
	assert(la != NULL);

	// Отправка сообщения
	msg_t* msg = new msg_t; // Создание сообщения
	assert(msg != NULL);
	msg->x = 300;
	la->run(msg); // Постановка в очередь на выполнение

	// Отправка сообщения необрабатываемого типа
	lite_msg_type<msg_bad_t>(); // Добавление "msg_bad_t" в кэш написаний
	msg_bad_t* msg_bad = new msg_bad_t; // Создание сообщения
	assert(msg_bad != NULL);
	msg_bad->x = 399;
	la->run(msg_bad); // Постановка в очередь на выполнение

	std::this_thread::sleep_for(std::chrono::seconds(3)); // для самоостановки потоков
	lite_thread_end(); // Ожидание завершения работы
}


int main() {

	test1();

	test2();

	test3();

#ifdef _DEBUG
	printf("Press any key ...\n");
	getchar();
#endif
	return 0;
}
