// Простейшие примеры использования с выводом всего происходящего в потоках в консоль
#define LT_DEBUG
#define LT_DEBUG_LOG
#include "../lite_thread.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// Эмуляция работы
void work(int n) {
	lite_log("thread#%d recv %d", (int)lite_thread_num(), n); // Начало обработки
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	lite_log("thread#%d end %d", (int)lite_thread_num(), n); // Конец
}

//------------------------------------------------------------------------
// Тест 1.
// 10 запусков подряд (в выводе recv 100+)
void actor1(lite_msg_t* msg, void* env) {// Обработчик сообщения
	uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x); // Обработка содержимого сообщения
}

void test1() { // Основной поток
	lite_log("--- test 1 ---");
	// Отправка 10 сообщений
	for (int i = 100; i < 110; i++) {
		lite_msg_t* msg = lite_msg_create<uint32_t>(); // Создание сообщения
		uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor1); // Отправка msg в actor1()
	}
	// Ожидание завершения работы
	lite_thread_end();
}

//------------------------------------------------------------------------
// Тест 2.
// 20 запусков подряд с обработкой в 3 потока (в выводе recv 200+)
void actor2(lite_msg_t* msg, void* env) {// Обработчик сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x); // Обработка содержимого сообщения
}

void test2() { // Основной поток
	lite_log("--- test 2 ---");
	lite_actor_parallel(3, actor2); // Глубина распараллеливания 3 потока
	for (int i = 200; i < 220; i++) {
		lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor2); // Отправка msg в actor2()
	}
	// Ожидание завершения работы
	lite_thread_end();
}

//------------------------------------------------------------------------
// Тест 3.
// 1 запуск и 10 рекурсивных вызовов (в выводе recv 300+)

class worker_t : public lite_worker_t {
	int count; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект

public:
	// Конструктор
	worker_t() {
		count = 0;
		type_add(lite_msg_type<uint32_t>()); // Разрешение принимать сообщение типа int
	}

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		work(*x);
		(*x)++; // Изменение сообщения
		count++; // Счетчик отправок
		if (count < 10) {
			lite_thread_run(msg, handle()); // Отправляем сообщение себе
		}
	}

	~worker_t() {
		// Нельзя использовать lite_log(), т.к. он отправляет сообщения
		printf("thread#%d worker end. count = %d\n", (int)lite_thread_num(), count);
	}
};

void test3() { // Основной поток
	lite_log("--- test 3 ---");
	lite_actor_t* la = lite_actor_create<worker_t>(); // Создание актора-объекта worker_t

	// Отправка сообщения
	lite_msg_t* msg = lite_msg_create<uint32_t>();// Создание сообщения
	uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
	*x = 300; // Установка содержимого сообщения
	lite_thread_run(msg, la); // Отправка сообщения на la

	// Отправка сообщения необрабатываемого типа
	lite_msg_type<int>(); // Для кэширования названия типа
	lite_msg_t* msg_err = lite_msg_create<int>();// Создание сообщения
	int* y = lite_msg_data<int>(msg_err); // Указатель на содержимое сообщения
	*y = 399; // Установка содержимого сообщения
	lite_thread_run(msg_err, la); // Отправка сообщения на la

	std::this_thread::sleep_for(std::chrono::seconds(2)); // для самоостановки потоков
	lite_thread_end(); // Ожидание завершения работы
}


int main()
{
	test1();

	test2();

	test3();

#ifdef _DEBUG
	printf("Press any key ...\n");
	getchar();
#endif
	return 0;
}
