#define DEBUG_LT
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
	printf("%5d: thread#%d work %d\n", time_now(), lite_thread_num(), n);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	printf("%5d: thread#%d end %d\n", time_now(), lite_thread_num(), n);
}

// Тест 1.
// 10 запусков подряд (work 100+)
lite_msg_t* actor1(lite_msg_t* msg, void* env) {// Обработчик сообщения
	uint32_t* x = lite_msg_data<uint32_t>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x); // работаем с содержимым
	return NULL;
}

void test1() { // Основной поток
			   // Отправка 10 сообщений
	for (int i = 100; i < 110; i++) {
		lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor1); // Отправка
	}
}

//------------------------------------------------------------------------
// Тест 2.
// 20 запусков подряд с обработкой в 3 потока (work 200+)
lite_msg_t* actor2(lite_msg_t* msg, void* env) {// Обработчик сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	work(*x);
	return NULL;
}

void test2() { // Основной поток
	lite_actor_parallel(3, actor2); // Глубина распараллеливания 3 потока
	for (int i = 200; i < 220; i++) {
		lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
		int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
		assert(x != NULL);
		*x = i;
		lite_thread_run(msg, actor2); // Отправка
	}
}

//------------------------------------------------------------------------
// Тест 3.
// 1 запуск и 10 рекурсивных вызовов (work 300+)
lite_msg_t* actor3(lite_msg_t* msg, void* env) {// Обработчик сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	(*x)++;
	work(*x);
	if ((*x) < 310) {
		return lite_msg_return(msg, actor3); // Отправляем сообщение себе
	}
	else {
		return NULL;
	}
}

lite_msg_t* actor3_end(lite_msg_t* msg, void* env) {// Обработчик окончания работы
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	printf("%5d: thread#%d finish %d\n", time_now(), lite_thread_num(), *x);
	return NULL;
}

void test3() { // Основной поток
	lite_msg_t* msg_end = lite_msg_create<int>();// Создание сообщения об окончании работы
	int* e = lite_msg_data<int>(msg_end); // Указатель на содержимое сообщения
	assert(e != NULL);
	*e = 399;
	lite_msg_end(msg_end, actor3_end); // Регистрация сообщения об окончании работы

	lite_msg_t* msg = lite_msg_create<int>();// Создание сообщения
	int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
	assert(x != NULL);
	*x = 300;
	lite_thread_run(msg, actor3);
}


int main()
{
	printf("%d %d\n", sizeof(lite_actor_t), sizeof(lite_thread_t));
	printf(" time: thread#N action\n");
	// Для запуска одного теста закамментить ненужные
	test1();
	test2();
	test3();
	//-------------
	std::this_thread::sleep_for(std::chrono::milliseconds(5000)); // для запуска потоков
	lite_thread_end();
	printf("Press any key ...\n");
	getchar();
	return 0;
}
