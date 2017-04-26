/* Тест работоспособности.
При успешном завершении выдает в конце "Test OK. worked: ... msg (min ... max ...)"

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
#define LT_DEBUG_LOG
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../lite_thread.h"
#include <atomic>
#include <vector>
#include <stdio.h>
#include <string.h>

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
class msg_t : public lite_msg_t {
public:
	size_t worker_num;		// Номер обработчика сообщения
	size_t rand;			// Для генерации следующего шага
	int count_all;		// Количество пройденных циклов
	size_t step_count;	// Количество пройденных шагов
	lite_actor_t* map[ACTOR_COUNT]; // Список акторов
	bool mark[ACTOR_COUNT]; // Отметка актора об обработке сообщения
};

//---------------------------------------------------------------------
// Обработчик ошибок и вывод лога
class log_t : public lite_actor_t {
	void recv(lite_msg_t* msg) override {
		lite_msg_log_t* m = dynamic_cast<lite_msg_log_t*>(msg);
		assert(m != NULL);
		printf("%s\n", m->data.c_str());
		if(m->err_num != 0) {
			stop_all = true;
		}
	}
};

//---------------------------------------------------------------------

class worker_t : public lite_actor_t {
	static std::atomic<int> worker_end; // Счетчик завершивших работу

	int count = 0; // Количество вызовов
	std::atomic<int> parallel = {0}; // Количество парралельных запусков
	// Указатель на актор конца обработки
	lite_actor_t* finish;

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		parallel++;
		if(parallel != 1) {
			lite_log(LITE_ERROR_USER, "parralel %d", (int)parallel);
			return;
		}
		msg_t* m = static_cast<msg_t*>(msg);

		if(m == NULL) { // Неверный тип сообщения
			lite_log(LITE_ERROR_USER, "wrong msg type");
			return;
		}
		if(m->worker_num < 0 || m->worker_num >= ACTOR_COUNT) { // Индекс за пределами массива
			lite_log(LITE_ERROR_USER, "worker_num = %d", (int)m->worker_num);
			return;
		}
		if(m->map[m->worker_num] != this) { // Сообщение пришло не тому обработчику
			lite_log(LITE_ERROR_USER, "wrong worker");
			return;
		}
		if (m->mark[m->worker_num]) { // Сообщение уже обработано этим обработчиком
			lite_log(LITE_ERROR_USER, "msg already worked");
			return;
		}
		// Отметка что актор пройден
		m->mark[m->worker_num] = true;
		m->step_count++;
		if(m->step_count >= STEP_COUNT) { 
			// Пройдено нужное количество шагов. Отправка на проверку
			finish->run(msg);
		} else {
			// Выбор следующего
			for(size_t i = 0; i < 5; i++) {
				m->rand = m->rand * 1023 + 65537;
				m->worker_num = m->rand % ACTOR_COUNT;
				if (!m->mark[m->worker_num]) break; // актор m->worker_num не пройден
			}
			if(m->mark[m->worker_num]) { // актор m->worker_num пройден
				// Поиск следующего непройденного
				for(size_t i = m->worker_num; i < ACTOR_COUNT; i++) {
					if(!m->mark[i]) { // Актор i не пройден
						m->worker_num = i;
						break;
					}
				}
			}
			if (m->mark[m->worker_num]) { // актор m->worker_num пройден
				// Поиск следующего непройденного
				for (size_t i = 0; i < m->worker_num; i++) {
					if (!m->mark[i]) { // Актор i не пройден
						m->worker_num = i;
						break;
					}
				}
			}

			// Отправка сообщения следующему
			m->map[m->worker_num]->run(msg);
		}
		count++;
		parallel--;
	}

public:
	// Конструктор
	worker_t() {
		finish = lite_actor_get("finish");
		assert(finish != NULL);
		count = 0;
		type_add(lite_msg_type<msg_t>());
	}

	// Завершение работы актора
	~worker_t() {
		msg_total += count;
		worker_end++;
		if (count == 0 && !stop_all) printf("WARNING: worker count = 0\n");
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
class start_t : public lite_actor_t {
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		if(m == NULL) {
			lite_log(LITE_ERROR_USER, "start: wrong msg type");
			return;
		}
		// Очистка отметок выполнения
		m->count_all++;
		m->rand = m->rand * 1023 + 65537;
		m->worker_num = m->rand % ACTOR_COUNT;
		m->step_count = 0;
		memset(m->mark, 0, sizeof(m->mark));
		// Отправка дальше
		m->map[m->worker_num]->run(msg);
	}
};

//---------------------------------------------------------------------
// Проверка заполнения сообщения
class finish_t : public lite_actor_t {
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		if (m == NULL) {
			lite_log(LITE_ERROR_USER, "finish: wrong msg type");
			return;
		}
		// Проверка прохождения всех обработчиков
		size_t count = 0;
		for(size_t i = 0; i < ACTOR_COUNT; i++) {
			if (m->mark[i]) count++;
		}
		if(count != STEP_COUNT) {
			lite_log(LITE_ERROR_USER, "skipped %d actors", (int)(ACTOR_COUNT - count));
			return;
		}

		msg_count++;

		int64_t time = lite_time_now();
		if(stop_all || time > TEST_TIME * 1000) {
			// Время теста истекло
			msg_finished++;
			if (msg_count_max < m->count_all) msg_count_max = m->count_all;
			if (msg_count_min > m->count_all) msg_count_min = m->count_all;
			return;
		} else if(time > time_alert) {
			// Вывод текущего состояния раз 0.5 сек
			time_alert += 500;
			lite_log(0, "%5lld: worked %d msg", lite_time_now(), (int)msg_count);
		}
		// Проверки пройдены, запуск следующего
		static lite_actor_t* start = NULL;
		if (start == NULL) start = lite_actor_get("start");
		start->run(msg);
		//lite_thread_run(msg, start);
	}
};


int main()
{
	// Установка обработчика ошибок
	log_t* log = new log_t;
	log->name_set("log");

	lite_log(0, "compile %s %s", __DATE__, __TIME__);
	lite_log(0, "START workers: %d  messages: %d  time: %d sec", ACTOR_COUNT, MSG_COUNT, TEST_TIME);

	// Инициализация указателей
	start_t* start = new start_t;
	start->name_set("start");
	start->parallel_set(5);

	finish_t* finish = new finish_t;
	finish->name_set("finish");
	finish->parallel_set(5);

	lite_actor_t* worker_list[ACTOR_COUNT];
	for(size_t i = 0; i < ACTOR_COUNT; i++) {
		worker_list[i] = new worker_t;
	}

	// Установка ограничения количества потоков
	lite_thread_max(CPU_MAX);

	// Создание сообщений
	for(size_t i = 0; i < MSG_COUNT; i++) {
		msg_t* msg = new msg_t();
		msg->rand = i;
		msg->count_all = 0;
		for (size_t j = 0; j < ACTOR_COUNT; j++) msg->map[j] = worker_list[j];

		start->run(msg);
	}
	
	lite_thread_end(); // Ожидание окончания расчета

	if(msg_finished != MSG_COUNT) {
		printf("ERROR: lost %d messages\n", MSG_COUNT - msg_finished);
	} else if (worker_t::count_end() != ACTOR_COUNT) {
		printf("ERROR: lost %d worker finish\n", ACTOR_COUNT - worker_t::count_end());
	}else if (msg_total != msg_count * STEP_COUNT) {
		printf("ERROR: total %d need %d\n", (int)msg_total, msg_count * STEP_COUNT);
	} else {
		printf("Test OK. worked: %d msg (min %d max %d)\n", (int)msg_count, (int)msg_count_min, (int)msg_count_max);
	}
	printf("compile %s %s with %s\n", __DATE__, __TIME__, LOCK_TYPE_LT);


#ifdef _DEBUG
	printf("Press any key ...");
	getchar();
#endif
	return 0;
}
