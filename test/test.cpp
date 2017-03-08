// Простейшие примеры использования с выводом всего происходящего в потоках в консоль
#define LT_DEBUG
#include "../lite_thread.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// Эмуляция работы
void work(int n) {
	printf("%5lld: thread#%d recv %d\n", lite_time_now(), (int)lite_thread_num(), n); // Начало обработки
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	printf("%5lld: thread#%d end %d\n", lite_time_now(), (int)lite_thread_num(), n); // Конец
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
	printf("%5lld: --- test 1 ---\n", lite_time_now());
	// Отправка 10 сообщений
	for (int i = 100; i < 110; i++) {
		lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor1); // Отправка msg в actor1()
	}
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
	printf("%5lld: --- test 2 ---\n", lite_time_now());
	lite_actor_parallel(3, actor2); // Глубина распараллеливания 3 потока
	for (int i = 200; i < 220; i++) {
		lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor2); // Отправка msg в actor2()
	}
}

//------------------------------------------------------------------------
// Тест 3.
// 1 запуск и 10 рекурсивных вызовов (в выводе recv 300+)
#define TYPE_DATA 1

class worker_t : public lite_worker_t {
	int count; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект

public:
	// Конструктор
	worker_t() {
		count = 0;
		i_am = handle(); // i_am актор из объекта класса worker_t
	}

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		switch (msg->type) {
		case TYPE_DATA:
			work_data(msg);
			return;

		default:
			printf("%5lld: thread#%d unknown type %d\n", lite_time_now(), (int)lite_thread_num(), msg->type);
		}
	}

	// Обработка сообщения TYPE_DATA
	void work_data(lite_msg_t* msg) {
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		work(*x);
		(*x)++; // Изменение сообщения
		count++; // Счетчик отправок
		if (count < 10) {
			lite_thread_run(msg, i_am); // Отправляем сообщение себе
		}
	}

	~worker_t() {
		printf("%5lld: thread#%d worker end. count = %d\n", lite_time_now(), (int)lite_thread_num(), count);
	}
};

void test3() { // Основной поток
	printf("%5lld: --- test 3 ---\n", lite_time_now());
	lite_actor_t* la = lite_actor_create<worker_t>(); // Создание актора-объекта worker_t

	lite_msg_t* msg = lite_msg_create<int>(TYPE_DATA);// Создание сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	*x = 300; // Установка содержимого сообщения
	lite_thread_run(msg, la); // Отправка сообщения на la
}


int main()
{
	printf(" time: thread#N action\n");

	test1();
	std::this_thread::sleep_for(std::chrono::seconds(2)); // для завершения теста

	test2();
	std::this_thread::sleep_for(std::chrono::seconds(3)); // для завершения теста

	test3();
	std::this_thread::sleep_for(std::chrono::seconds(2)); // для завершения теста

	// Ожидание завершения работы
	lite_thread_end();
#ifdef _DEBUG
	printf("Press any key ...\n");
	getchar();
#endif
	return 0;
}
