/* Тест работоспособности

Создается ACTOR_COUNT акторов обработчиков для каждого сообщения.
Запускается MSG_COUNT сообщений (от количества сообщений зависит сколько максимум потоков потребуется)

Каждое сообщение содержит маршрут прохождения (последовательность акторов, которые надо надо пройти)
У каждого сообщения каждый MSG_COUNT*3 актор совпадает с актором другого сообщения

Каждый актор ставит свой флаг обработки. По окончании проверка что все акторы пройдены и запуск нового.

Сообщения гоняются по кругу TEST_TIME секунд
*/

#define ACTOR_COUNT 100  // Количество обработчиков
#define MSG_COUNT	3   // Количество одновременно идущих сообщений
#ifdef _DEBUG
#define TEST_TIME	3  // Время теста, сек.
#else
#define TEST_TIME	10  // Время теста, сек.
#endif
//---------------------------------------------------------------------
#define DEBUG_LT
#define STAT_LT
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
// Содержимое сообщения
struct data_t {
	int worker_num;	// Номер обработчика сообщения
	lite_actor_t* map[ACTOR_COUNT]; // Карта прохождения сообщения
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
		return;
	}

	// Обработка сообщения
	void work(lite_msg_t* msg) {
		parallel++;
		if(parallel != 1) {
			printf("ERROR: parralel %d\n", (int)parallel);
			return;
		}
		data_t* d = lite_msg_data<data_t>(msg); // Указатель на содержимое сообщения
		if(d == NULL) { // Неверный размер сообщения
			printf("ERROR: wrong msg size\n");
			return;
		}
		if(d->worker_num < 0 || d->worker_num >= ACTOR_COUNT) { // Индекс за пределами массива
			printf("ERROR: worker_num = %d\n", d->worker_num);
			return;
		}
		if(d->map[d->worker_num] != i_am) { // Сообщение пришло не тому обработчику
			printf("ERROR: wrong worker\n");
			return;
		}
		if (d->mark[d->worker_num]) { // Сообщение уже обработано этим обработчиком
			printf("ERROR: msg already worked\n");
			return;
		}
		d->mark[d->worker_num] = true;
		d->worker_num++;
		if(d->worker_num < ACTOR_COUNT) {
			// Отправка сообщения следующему
			lite_thread_run(msg, d->map[d->worker_num]); 
		} else {
			// Отправка на проверку
			lite_thread_run(msg, finish);
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
			printf("%5d: thread#%d unknown msg type %d\n", time_now(), lite_thread_num(), msg->type);
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

std::atomic<int> worker_t::worker_end = { 0 };
//---------------------------------------------------------------------
// Массив акторов обработчиков
std::vector<worker_t> worker_list(ACTOR_COUNT * MSG_COUNT);

std::atomic<int> msg_count = { 0 }; // Счетчик сообщений дошедших до финиша
std::atomic<int> msg_finished = { 0 }; // Счетчик сообщений пришедших после остановки теста
std::atomic<int> time_alert = { 500 }; // Время следующего вывода состояния теста


//---------------------------------------------------------------------
// Подготовка сообщения и отправка на обработку
void start_func(lite_msg_t* msg, void* env) {
	data_t* d = lite_msg_data<data_t>(msg);  // Указатель на содержимое сообщения
	if(d == NULL || msg->type != TYPE_DATA) {
		printf("ERROR: wrong msg size or type\n");
		return;
	}
	// Проверка что все отметки поставлены
	int count = 0;
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		if (d->mark[i]) count++;
	}
	if(count != ACTOR_COUNT) {
		printf("ERROR: lost %d actors mark\n", ACTOR_COUNT - count);
		return;
	}
	// Очистка отметок выполнения
	d->worker_num = 0;
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
		return;
	}
	// Проверка прохождения всех обработчиков
	size_t count = 0;
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		if (d->mark[i]) count++;
	}
	if(count != ACTOR_COUNT) {
		printf("ERROR: skipped %d actors\n", ACTOR_COUNT - count);
		return;
	}

	msg_count++;

	int time = time_now();
	if(time > TEST_TIME * 1000) {
		// Время теста истекло
		msg_finished++;
		return;
	} else if(time > time_alert) {
		// Вывод текущего состояния раз 0.5 сек
		time_alert += 500;
		printf("%5d: worked %d msg\n", time_now(), (int)msg_count);
	}
	// Проверки пройдены, запуск следующего
	lite_thread_run(msg, start);
}

int main()
{
	printf("compile %s %s\n", __DATE__, __TIME__);
	printf("%5d: START workers: %d  messages: %d  time: %d sec\n", time_now(), ACTOR_COUNT, MSG_COUNT, TEST_TIME);
	// Инициализация указателей
	start = lite_actor_get(start_func);
	finish = lite_actor_get(finish_func);

	// Создание сообщений
	lite_msg_t* msg[MSG_COUNT];
	data_t* d[MSG_COUNT];
	for(size_t i = 0; i < MSG_COUNT; i++) {
		msg[i] = lite_msg_create<data_t>(TYPE_DATA);
		d[i] = lite_msg_data<data_t>(msg[i]);  // Указатель на содержимое сообщения
		memset(d[i]->mark, 1, sizeof(data_t::mark));
	}
	// Распределение обработчиков по сообщениям
	for (size_t i = 0; i < MSG_COUNT; i++) {
		for (size_t j = 0; j < ACTOR_COUNT; j++) {
			d[i]->map[j] = worker_list[j*MSG_COUNT + i].handle();
		}
	}
	// Пересечение потоков на каждом MSG_COUNT*3 обработчике
	for (size_t j = 0; j < ACTOR_COUNT - MSG_COUNT; j += MSG_COUNT * 3) {
		for (size_t i = 0; i < MSG_COUNT - 1; i++) {
			d[i]->map[j + i] = d[i + 1]->map[j + i];
		}
	}
	// Отправка сообщений
	for (size_t i = 0; i < MSG_COUNT; i++) {
		lite_thread_run(msg[i], start);
	}

	std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // для запуска потоков
	lite_thread_end();

	int total = 0;
	for(size_t i = 0; i < ACTOR_COUNT * MSG_COUNT; i++) {
		total += worker_list[i].count_msg();
	}

	if(msg_finished != MSG_COUNT) {
		printf("ERROR: lost %d messages\n", MSG_COUNT - msg_finished);
	} else if (worker_t::count_end() != ACTOR_COUNT * MSG_COUNT) {
		printf("ERROR: lost %d worker finish\n", ACTOR_COUNT * MSG_COUNT - worker_t::count_end());
	}else if (total != msg_count * ACTOR_COUNT) {
			printf("ERROR: total %d need %d\n", total, msg_count * ACTOR_COUNT);
	} else {
		printf("%5d: test OK worked: %d msg  transfer: %d msg  MSG_COUNT: %d\n", time_now(), (int)msg_count, total, MSG_COUNT);
	}
#ifdef _DEBUG
	printf("Press any key ...\n");
	getchar();
#endif

	//delete[] worker_list;
	return 0;
}
