#define DEBUG_LT
#define STAT_LT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../lite_thread.h"
#include <stdio.h>
#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
//#include <time.h>

void work(int n) { // Эмуляция работы
	printf("%5d: thread#%d recv %d\n", time_now(), lite_thread_num(), n); // Начало обработки
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	printf("%5d: thread#%d end %d\n", time_now(), lite_thread_num(), n); // Конец
}

// Тест 1.
// 10 запусков подряд (work 100+)
void actor1(lite_msg_t* msg, void* env) {// Обработчик сообщения
	uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x); // работаем с содержимым
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
// 20 запусков подряд с обработкой в 3 потока (work 200+)
void actor2(lite_msg_t* msg, void* env) {// Обработчик сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x);
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
// 1 запуск и 10 рекурсивных вызовов (work 300+)
#define TYPE_DATA 1
#define TYPE_END  2

class worker_t {
	int count; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект
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

	static void recv(lite_msg_t* msg, void* env) {
		worker_t* w = (worker_t*)env;
		switch(msg->type) {
		case TYPE_DATA:
		{
			int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
			assert(x != NULL);
			printf("%5d: thread#%d recv %d\n", time_now(), lite_thread_num(), *x);
			(*x)++; // Изменение сообщения
			w->count++; // Счетчик отправок
			if (w->count < 10) {
				lite_thread_run(msg, w->i_am); // Отправляем сообщение себе
			}
			return;
		}

		case TYPE_END:
			printf("%5d: thread#%d work end. count = %d\n", time_now(), lite_thread_num(), w->count);
			return;

		default:
			printf("%5d: thread#%d unnown type %d\n", time_now(), msg->type);
		}
	}
};


int main()
{
	printf("%d %d\n", sizeof(lite_actor_t), sizeof(lite_thread_t));
	printf(" time: thread#N action\n");
	// Для запуска одного теста закамментить ненужные
	test1();
	test2();
	worker_t w; // это test3();
	//-------------
	std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // для запуска потоков
	lite_thread_end();
	printf("Press any key ...\n");
	getchar();
	return 0;
}
