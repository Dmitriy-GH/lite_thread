/* Тест работоспособности

Создается ACTOR_COUNT акторов обработчиков для каждого сообщения.
Запускается MSG_COUNT сообщений (от количества сообщений зависит сколько максимум потоков потребуется)

Каждое сообщение содержит карту акторов и отметки прохождения акторов, при очередной пересылке 
случайным образом выбирается следующий непройденный актор и пересылается ему. 

Каждый актор ставит свой флаг обработки. По прохождению STEP_COUNT акторов сообщение отправляется на финиш.
На финише проверка что все акторы пройдены и запуск нового сообщения.

Сообщения гоняются по кругу TEST_TIME секунд
*/

#define ACTOR_COUNT 1000  // Количество обработчиков
#define STEP_COUNT  100  // Количество шагов, которое должно пройти сообщение
#define MSG_COUNT	100  // Количество одновременно идущих сообщений
#ifdef _DEBUG
#define TEST_TIME	3  // Время теста, сек.
#else
#define TEST_TIME	10  // Время теста, сек.
#endif
//---------------------------------------------------------------------
//#define DEBUG_LT
//#define STAT_LT
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
#define TYPE_END   2

//---------------------------------------------------------------------
class worker_t;

std::vector<worker_t> worker_list(ACTOR_COUNT); // Массив акторов обработчиков

std::atomic<int> msg_count = { 0 }; // Счетчик сообщений дошедших до финиша
std::atomic<int> msg_finished = { 0 }; // Счетчик сообщений пришедших после остановки теста
std::atomic<int> time_alert = { 500 }; // Время следующего вывода состояния теста
std::atomic<bool> stop_all = { 0 }; // Флаг завершения работы


//---------------------------------------------------------------------
// Многопоточный ГСЧ
size_t lite_random() {
	static std::atomic<size_t> n = {0};
	thread_local size_t nt = {0};
	if(nt == 0) {
		size_t old = n;
		while (!n.compare_exchange_weak(old, old * 1023 + 65537));
		nt = old;
		//printf("init rand\n");
	} else {
		nt = nt * 1023 + 65537;
	}
	return nt;
}

//---------------------------------------------------------------------
// Содержимое сообщения
struct data_t {
	size_t worker_num;		// Номер обработчика сообщения
	size_t step_count;	// Количество пройденных шагов
	lite_actor_t* map[ACTOR_COUNT]; // Список акторов
	bool mark[ACTOR_COUNT]; // Отметка актора об обработке сообщения
};

//---------------------------------------------------------------------
// Указатели на акторы начала и конца обработки
lite_actor_t* finish;
lite_actor_t* start;

class alignas(64) worker_t {
	static std::atomic<int> worker_end; // Счетчик завершивших работу

	int count = 0; // Количество вызовов
	lite_actor_t* i_am; // Актор указывающий на этот объект
	std::atomic<int> parallel = {0}; // Количество парралельных запусков

	// Завершение работы актора
	void end() {
		worker_end++;
		#ifdef _DEBUG
		//printf("%d,", count);
		#endif
		if(count == 0) printf("WARNING: worker count = 0\n");
		return;
	}

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
				d->worker_num = lite_random() % ACTOR_COUNT;
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

	static void recv(lite_msg_t* msg, void* env) {
		worker_t* w = (worker_t*)env;
		switch (msg->type) {
		case TYPE_DATA:
			w->work(msg);
			break;

		case TYPE_END:
			w->end();
			return;

		default:
			printf("ERROR: thread#%d unknown msg type %d\n", (int)lite_thread_num(), msg->type);
			stop_all = true;
		}
	}

public:
	// Конструктор
	worker_t() {
		count = 0;
		i_am = lite_actor_get(worker_t::recv, this); // i_am актор из объекта класса worker_t
		// Регистрация оповещения об остановке. Будет получено перед окончанием работы приложения.
		lite_msg_t* msg_end = lite_msg_create(0, TYPE_END); // Создание сообщения об окончании работы
		lite_msg_end(msg_end, i_am); // Регистрация сообщения об окончании работы
	}

	// Указатель на обработчик
	lite_actor_t* handle() {
		return i_am;
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
	d->worker_num = rand() % ACTOR_COUNT;
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
		return;
	} else if(time > time_alert) {
		// Вывод текущего состояния раз 0.5 сек
		time_alert += 500;
		printf("%5lld: worked %d msg\n", lite_time_now(), (int)msg_count);
	}
	// Проверки пройдены, запуск следующего
	lite_thread_run(msg, start);
}

int main()
{
	printf("compile %s %s\n", __DATE__, __TIME__);
	printf("%5lld: START workers: %d  messages: %d  time: %d sec\n", lite_time_now(), ACTOR_COUNT, MSG_COUNT, TEST_TIME);
	// Инициализация указателей
	start = lite_actor_get(start_func);
	lite_actor_parallel(5, start);
	finish = lite_actor_get(finish_func);
	lite_actor_parallel(5, finish);

	// Создание сообщений
	for(size_t i = 0; i < MSG_COUNT; i++) {
		lite_msg_t* msg;
		msg = lite_msg_create<data_t>(TYPE_DATA);
		data_t* d = lite_msg_data<data_t>(msg);  // Указатель на содержимое сообщения
		for(size_t j = 0; j < ACTOR_COUNT; j++) d->map[j] = worker_list[j].handle();
		lite_thread_run(msg, start);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // для запуска потоков
	lite_thread_end(); // Ожидание окончания

	int total = 0;
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		total += worker_list[i].count_msg();
	}

	if(msg_finished != MSG_COUNT) {
		printf("ERROR: lost %d messages\n", MSG_COUNT - msg_finished);
	} else if (worker_t::count_end() != ACTOR_COUNT) {
		printf("ERROR: lost %d worker finish\n", ACTOR_COUNT - worker_t::count_end());
	}else if (total != msg_count * STEP_COUNT) {
			printf("ERROR: total %d need %d\n", total, msg_count * STEP_COUNT);
	} else {
		printf("%5lld: test OK worked: %d msg  transfer: %d msg  MSG_COUNT: %d\n", lite_time_now(), (int)msg_count, total, MSG_COUNT);
	}
	printf("compile %s %s with %s\n", __DATE__, __TIME__, LOCK_TYPE_LT);
#ifdef _DEBUG
	printf("Press any key ...\n");
	getchar();
#endif
	return 0;
}
