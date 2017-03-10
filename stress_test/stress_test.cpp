/* Тест работоспособности.
При успешном завершении выдает в конце "... test OK ..."

Создается ACTOR_COUNT акторов обработчиков для каждого сообщения.
Запускается MSG_COUNT сообщений (от количества сообщений зависит сколько максимум потоков потребуется)

Каждое сообщение содержит карту акторов и отметки прохождения акторов, при очередной пересылке 
случайным образом выбирается следующий непройденный актор и пересылается ему. 

Каждый актор ставит свой флаг обработки. По прохождению STEP_COUNT акторов сообщение отправляется на финиш.
На финише проверка что все акторы пройдены и запуск нового сообщения.

Сообщения гоняются по кругу TEST_TIME секунд
*/

#ifndef _DEBUG
#define ACTOR_COUNT 1000  // Количество обработчиков
#define STEP_COUNT  100  // Количество шагов, которое должно пройти сообщение
#define MSG_COUNT	100  // Количество одновременно идущих сообщений
#define TEST_TIME	10  // Время теста, сек.
#else
#define ACTOR_COUNT 100
#define STEP_COUNT  10
#define MSG_COUNT	2
#define TEST_TIME	3
#endif

#define CPU_MAX 8 // Максимальное количество одновременно работающих потоков
//---------------------------------------------------------------------
//#define LT_DEBUG
#define LT_STAT
//#define LT_STAT_QUEUE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../lite_thread.h"
#include <atomic>
#include <vector>
#include <stdio.h>
#include <string.h>

//---------------------------------------------------------------------
// Типы сообщений
#define TYPE_DATA  1

//---------------------------------------------------------------------
std::atomic<int> msg_count = { 0 }; // Счетчик сообщений дошедших до финиша
std::atomic<int> msg_total = { 0 }; // Счетчик сообщений прошедших через обработчика
std::atomic<int> msg_count_min = { 999999999 }; // Мин. количество кругов пройденных одним сообщением
std::atomic<int> msg_count_max = { 0 }; // Макс. количество кругов пройденных одним сообщением
std::atomic<int> msg_finished = { 0 }; // Счетчик сообщений пришедших после остановки теста
std::atomic<int> time_alert = { 500 }; // Время следующего вывода состояния теста
std::atomic<bool> stop_all = { 0 }; // Флаг завершения работы

//---------------------------------------------------------------------
// Содержимое сообщения
struct data_t {
	lite_actor_t* i_am;
	size_t worker_num;		// Номер обработчика сообщения
	size_t rand;			// Для генерации следующего шага
	int count_all;		// Количество пройденных циклов
	size_t step_count;	// Количество пройденных шагов
	lite_actor_t* map[ACTOR_COUNT]; // Список акторов
	bool mark[ACTOR_COUNT]; // Отметка актора об обработке сообщения
};

//---------------------------------------------------------------------
// Указатели на акторы начала и конца обработки
lite_actor_t* finish;
lite_actor_t* start;

class alignas(64) worker_t : public lite_worker_t {
	static std::atomic<int> worker_end; // Счетчик завершивших работу

	int count = 0; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект
	std::atomic<int> parallel = {0}; // Количество парралельных запусков

	// Обработка сообщения
	void work(lite_msg_t* msg) {
		parallel++;
		if(parallel != 1) {
			printf("ERROR: parralel %d\n", (int)parallel);
			stop_all = true;
			return;
		}
		data_t* d = lite_msg_data<data_t>(msg); // Указатель на содержимое сообщения
		if(d == NULL) { // Неверный размер сообщения
			printf("ERROR: wrong msg size\n");
			stop_all = true;
			return;
		}
		if(d->worker_num < 0 || d->worker_num >= ACTOR_COUNT) { // Индекс за пределами массива
			printf("ERROR: worker_num = %d\n", (int)d->worker_num);
			stop_all = true;
			return;
		}
		if(d->map[d->worker_num] != i_am) { // Сообщение пришло не тому обработчику
			printf("ERROR: wrong worker\n");
			stop_all = true;
			return;
		}
		if (d->mark[d->worker_num]) { // Сообщение уже обработано этим обработчиком
			printf("ERROR: msg already worked\n");
			stop_all = true;
			return;
		}
		// Отметка что актор пройден
		d->mark[d->worker_num] = true;
		d->step_count++;
		if(d->step_count >= STEP_COUNT) { 
			// Пройдено нужное количество шагов. Отправка на проверку
			lite_thread_run(msg, finish);
		} else {

			// Выбор следующего
			for(size_t i = 0; i < 5; i++) {
				d->rand = d->rand * 1023 + 65537;
				d->worker_num = d->rand % ACTOR_COUNT;
				if (!d->mark[d->worker_num]) break; // актор d->worker_num не пройден
			}
			if(d->mark[d->worker_num]) { // актор d->worker_num пройден
				// Поиск следующего непройденного
				for(size_t i = d->worker_num; i < ACTOR_COUNT; i++) {
					if(!d->mark[i]) { // Актор i не пройден
						d->worker_num = i;
						break;
					}
				}
			}
			if (d->mark[d->worker_num]) { // актор d->worker_num пройден
				// Поиск следующего непройденного
				for (size_t i = 0; i < d->worker_num; i++) {
					if (!d->mark[i]) { // Актор i не пройден
						d->worker_num = i;
						break;
					}
				}
			}

			// Отправка сообщения следующему
			lite_thread_run(msg, d->map[d->worker_num]); 
		}
		count++;
		parallel--;
	}

	void recv(lite_msg_t* msg) override {
		switch (msg->type) {
		case TYPE_DATA:
			work(msg);
			break;

		default:
			printf("ERROR: thread#%d unknown msg type %d\n", (int)lite_thread_num(), msg->type);
			stop_all = true;
		}
	}

public:
	// Конструктор
	worker_t() {
		i_am = handle();
		count = 0;
	}

	// Завершение работы актора
	~worker_t() {
		msg_total += count;
		worker_end++;
		if (count == 0) printf("WARNING: worker count = 0\n");
		return;
	}

	// Количество обработанных сообщений
	int count_msg() {
		return count;
	}
	// Количество завершивших работу
	static int count_end() {
		return worker_end;
	}
};
std::atomic<int> worker_t::worker_end = { 0 };  // Счетчик завершивших работу 

//---------------------------------------------------------------------
// Подготовка сообщения и отправка на обработку
void start_func(lite_msg_t* msg, void* env) {
	data_t* d = lite_msg_data<data_t>(msg);  // Указатель на содержимое сообщения
	if(d == NULL || msg->type != TYPE_DATA) {
		printf("ERROR: wrong msg size or type\n");
		stop_all = true;
		return;
	}
	// Очистка отметок выполнения
	d->count_all++;
	d->rand = d->rand * 1023 + 65537;
	d->worker_num = d->rand % ACTOR_COUNT;
	d->step_count = 0;
	memset(d->mark, 0, sizeof(d->mark));
	// Отправка дальше
	lite_thread_run(msg, d->map[d->worker_num]);
}


//---------------------------------------------------------------------
// Проверка заполнения сообщения
void finish_func(lite_msg_t* msg, void* env) {
	if(msg->type != TYPE_DATA) {
		printf("ERROR: wrong msg type\n");
		return;
	}
	data_t* d = lite_msg_data<data_t>(msg); // Указатель на содержимое сообщения
	if (d == NULL) { // Неверный размер сообщения
		printf("ERROR: wrong msg size\n");
		stop_all = true;
		return;
	}
	// Проверка прохождения всех обработчиков
	size_t count = 0;
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		if (d->mark[i]) count++;
	}
	if(count != STEP_COUNT) {
		printf("ERROR: skipped %d actors\n", (int)(ACTOR_COUNT - count));
		stop_all = true;
		return;
	}

	msg_count++;

	int64_t time = lite_time_now();
	if(stop_all || time > TEST_TIME * 1000) {
		// Время теста истекло
		msg_finished++;
		if (msg_count_max < d->count_all) msg_count_max = d->count_all;
		if (msg_count_min > d->count_all) msg_count_min = d->count_all;
		return;
	} else if(time > time_alert) {
		// Вывод текущего состояния раз 0.5 сек
		time_alert += 500;
		lite_log("%5lld: worked %d msg", lite_time_now(), (int)msg_count);
	}
	// Проверки пройдены, запуск следующего
	lite_thread_run(msg, start);
}

int main()
{
	lite_log("compile %s %s", __DATE__, __TIME__);
	lite_log("START workers: %d  messages: %d  time: %d sec", ACTOR_COUNT, MSG_COUNT, TEST_TIME);
	// Инициализация указателей
	start = lite_actor_get(start_func);
	lite_actor_parallel(5, start);
	finish = lite_actor_get(finish_func);
	lite_actor_parallel(5, finish);

	lite_actor_t* worker_list[ACTOR_COUNT];
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		worker_list[i] = lite_actor_create<worker_t>();
	}

	// Установка ресурса "CPU"
	lite_resource_t* res = lite_resource_create("CPU", CPU_MAX);
	for (size_t i = 0; i < ACTOR_COUNT; i++) {
		worker_list[i]->resource_set(res);
	}
	//lite_resource_t* res2 = lite_resource_create("CPU2", 2);
	lite_resource_set("CPU", start);
	lite_resource_set("CPU", finish);
	lite_resource_set("CPU", lite_actor_get("log"));

	// Создание сообщений
	for(size_t i = 0; i < MSG_COUNT; i++) {
		lite_msg_t* msg;
		msg = lite_msg_create<data_t>(TYPE_DATA);
		data_t* d = lite_msg_data<data_t>(msg);  // Указатель на содержимое сообщения
		d->rand = i;
		d->count_all = 0;
		for(size_t j = 0; j < ACTOR_COUNT; j++) d->map[j] = worker_list[j];
		lite_thread_run(msg, start);
	}
	
	lite_thread_end(); // Ожидание окончания расчета

	if(msg_finished != MSG_COUNT) {
		printf("ERROR: lost %d messages\n", MSG_COUNT - msg_finished);
	} else if (worker_t::count_end() != ACTOR_COUNT) {
		printf("ERROR: lost %d worker finish\n", ACTOR_COUNT - worker_t::count_end());
	}else if (msg_total != msg_count * STEP_COUNT) {
		printf("ERROR: total %d need %d\n", (int)msg_total, msg_count * STEP_COUNT);
	} else {
		printf("%5lld: test OK worked: %d msg (min %d max %d) transfer: %d msg/sec.  MSG_COUNT: %d\n", lite_time_now(), (int)msg_count, (int)msg_count_min, (int)msg_count_max, (int)msg_total / TEST_TIME, MSG_COUNT);
	}
	printf("compile %s %s with %s\n", __DATE__, __TIME__, LOCK_TYPE_LT);


#ifdef _DEBUG
	printf("Press any key ...");
	getchar();
#endif
	return 0;
}
