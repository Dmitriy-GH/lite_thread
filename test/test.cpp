// Простейшие примеры использования с выводом всего происходящего в потоках в консоль

#define DEBUG_LT
#include "../lite_thread.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>

// Эмуляция работы
void work(int n) {
	printf("%5d: thread#%d recv %d\n", time_now(), lite_thread_num(), n); // Начало обработки
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	printf("%5d: thread#%d end %d\n", time_now(), lite_thread_num(), n); // Конец
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
#define TYPE_END  2

class worker_t {
	int count; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект

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

	// Обработка сообщения TYPE_END (оповещение о завершении работы)
	void end() {
		printf("%5d: thread#%d work end. count = %d\n", time_now(), lite_thread_num(), count);
	}

public:
	// Конструктор
	worker_t() {
		count = 0;
		i_am = lite_actor_get(worker_t::recv, this); // i_am актор из объекта класса worker_t
		// Регистрация оповещения об остановке. Будет получено перед окончанием работы приложения.
		lite_msg_t* msg_end = lite_msg_create(0, TYPE_END);// Создание сообщения об окончании работы
		lite_msg_end(msg_end, i_am); // Регистрация сообщения об окончании работы
		// Отправка сообщения себе 
		lite_msg_t* msg = lite_msg_create<int>(TYPE_DATA);// Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = 300; // Установка содержимого сообщения
		lite_thread_run(msg, i_am); // Отправка сообщения 
	}

	// Прием входящего сообщения
	static void recv(lite_msg_t* msg, void* env) {
		worker_t* w = (worker_t*)env;
		switch(msg->type) {
		case TYPE_DATA:
			w->work_data(msg);
			return;

		case TYPE_END:
			w->end();
			return;

		default:
			printf("%5d: thread#%d unknown type %d\n", time_now(), lite_thread_num(), msg->type);
		}
	}
};


int main()
{
	printf("%d %d\n", sizeof(lite_actor_t), sizeof(lite_thread_t));
	printf(" time: thread#N action\n");
	// Для запуска одного теста закамментить ненужные
	printf("%5d: --- test 1 ---\n", time_now());
	test1();
	std::this_thread::sleep_for(std::chrono::seconds(2)); // для завершения теста
	printf("%5d: --- test 2 ---\n", time_now());
	test2();
	std::this_thread::sleep_for(std::chrono::seconds(3)); // для завершения теста
	printf("%5d: --- test 3 ---\n", time_now());
	worker_t w;
	std::this_thread::sleep_for(std::chrono::seconds(2)); // для завершения теста
	lite_thread_end();
	printf("Press any key ...\n");
	getchar();
	return 0;
}
