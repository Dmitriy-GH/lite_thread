#pragma once
/* lite_thread.h

Кросплатформенная библиотека на С++11 для легкого распараллеливания кода, в т.ч. потокоНЕбезопасного.

Библиотека построена по Модели Акторов, т.е. общий алгоритм разбивается на акторы (код, атомарные
части бизнес-логики) и акторы общаются между собой пересылкой сообщений друг-другу.

Библиотека сама создает нужное количество потоков и запускает в них акторы.

Библиотека гарантирует что актор не будет запущен одновременно в нескольких потоках, т.е. код актора
не требует потокобезопасности. Но если актор потокобезопасный, то есть установка ограничения на
количество одновременно работающих копий данного актора. Также есть ограничение доступа группы акторов
к конкретному ресурсу (например для ограничения количества используемых потоков)


ПРИМЕР ПРОСТЕЙШЕГО ИСПОЛЬЗОВАНИЯ -------------------------------------------------------------

void actor(lite_msg_t* msg, void* env) {
	// Обработчик сообщения msg
}

void main() {
lite_msg_t* msg = lite_msg_create<int>(); // Создание сообщения
int* x = lite_msg_data<int>(msg); // Указатель на содержимое сообщения
*x = 100500; // Заполнение сообщения
lite_thread_run(msg, actor); //Отправка msg в actor()

lite_thread_end(); // Ожидание завершения работы
}


СООБЩЕНИЯ ------------------------------------------------------------------------------------

--- Создание сообщения.
Нетипизированное:
lite_msg_t* lite_msg_create(size_t size, size_t type = 0) // size размер в байтах
Типизированное:
lite_msg_t* lite_msg_create<T>() // size и type ставятся автоматически

--- Приведение указателя на содержимое к нужному типу.
T* lite_msg_data<T>(lite_msg_t* msg) // Вернет NULL при несовпадении размера типа и сообщения

--- Передача сообщения на обработку.
По хэндлу актора
lite_thread_run(lite_msg_t* msg, lite_actor_t* la)
По адресу функции актора
lite_thread_run(lite_msg_t* msg, lite_func_t func, void* env = NULL)
этот способ не рекомендуется многократно использовать, т.к. каждый раз будет происходить поиск
хэндла актора в индексе.

--- Копирование сообщения.
lite_msg_t* lite_msg_copy(lite_msg_t* msg)
При копировании сообщения полученного извне не использовать и не отправлять исходное, т.к. оно
не копируется, а просто снимается с контроля автоудаления. 
При копировании копии или явно созданного создается полноценная копия, т.е. можно использовать оба.

--- Удаление сообщения.
lite_msg_erase(lite_msg_t* msg)
Полученные извне и отправленные сообщения удалять нельзя, т.к. сообщения удаляются автоматически после 
обработки.
Удалять необходимо только скопированные или созданные и не отправленные сообщения.


ВАЖНО -----------------------------------------------------------------------------------------

Сообщения НЕ иммутабельны, поэтому:
- нельзя отправлять одно сообщение дважды;
- нельзя читать/писать сообщение после отправки, т.к. оно может быть уже в обработке или удалено.

Сообщение можно изменять и отправлять дальше.

Гарантируется что одно сообщение в любой момент времени обрабатывается только в одном потоке.

Гарантируется что каждый обработчик получит сообщения в том порядке, в котором они отправлены.


АКТОР ФУНКЦИЯ ---------------------------------------------------------------------------------

--- Тип функции
void func(lite_msg_t* msg, void* env)

--- Получение хэндла актора
lite_actor_t* lite_actor_get(lite_func_t func, void* env = NULL)
Каждая пара func+env сохраняется в один актор lite_actor_t, расположение актора в памяти постоянно,
поэтому для ускорения работы в коде лучше использовать хэндл, т.к. иначе каждый раз происходит
поиск хэндла в индексе. 

--- Присвоение имени
lite_actor_name(lite_actor_t* la, const std::string& name)


АКТОР КЛАСС -----------------------------------------------------------------------------------

Базовый класс lite_worker_t это обертка над актором-фукцией и хранилище объектов.
В дочернем классе необходимо прописать только метод recv()

class my_worker_t : public lite_worker_t {
	void recv(lite_msg_t* msg) override {
	}
}

Доступные методы базового класса:

--- Получение хэндла актора
lite_actor_t* handle()

--- Регистрация типа обрабатываемого сообщения
type_add(lite_msg_type<T>())
Если зарегистрирован хоть один тип сообщения, то в метод recv() будут передаваться только зарелистрированные, 
остальные будут игнорироваться с сообщением об ошибке.
Если ни один тип не зарегистрирован, то на обработку проходят все входящие сообщения.

--- Создание объекта
lite_actor_t* lite_actor_create<my_worker_t>()

--- Создание именованного объекта
lite_actor_t* lite_actor_create<my_worker_t>("my name")

Объекты создаются и сохраняются в кэше библиотеки. По окончанию работы уничтожаются автоматически.


ИМЕНОВАННЫЕ АКТОРЫ ---------------------------------------------------------------------------

--- Получение хэндла по имени
lite_actor_t* lite_actor_get(const std::string& name)


РАСПАРЕЛЛЕЛИВАНИЕ АКТОРА --------------------------------------------------------------------

Для акторов с потокобезопасным кодом (например актор без окружения, все необходимое для его
работы содержится в сообщении) можно установить глубину распараллеливания, т.е. в скольки
потоках актор может одновременно обрабатывать сообщения.

--- Установка глубины распараллеливания
lite_actor_parallel(int max, lite_actor_t* la)
lite_actor_parallel(int max, lite_func_t func, void* env = NULL)


РЕСУРСЫ --------------------------------------------------------------------------------------

Ресурс ограничивает сколько может быть запущено одновременно акторов привязанных к ресурсу.
При старте создается ресурс по умолчанию и вновь создаваемые акторы привязываются к нему.
Рекомендуется использовать ресурс по умолчанию для акторов нагружающих процессор. Соответственно 
максимум этого ресурса - количество потоков, которое можно использовать.
Для акторов не нагружающих процессор (например запрос к вэб-сервису и ожидание ответа) лучше 
создать отдельный ресурс и привязать туда эти акторы.

--- Установка ресурса по умолчанию
lite_thread_max(int max)

--- Создание ресурса
lite_resource_t* lite_resource_create(const std::string& name, int max)

--- Привязка актора к ресурсу
lite_actor_t* actor->resource_set(lite_resource_t* res)
lite_resource_set(const std::string& name, lite_actor_t* la)
lite_resource_set(const std::string& name, lite_func_t func, void* env = NULL)


ВЕДЕНИЕ ЛОГА --------------------------------------------------------------------------------

--- Запись в лог
lite_log(const char* data, ...)
К введенным данным добавляется дата-время и отправляется сообщением на актор с именем "log", где
по умолчанию выводится в консоль.

При необходимости можно создать свой актор "log". Он должен быть зарегистрирован до первого 
вызова lite_log(). Актор получает нетипизированные сообщения с текстом. msg->data текст.

Актор "log" нельзя создавать с помощью lite_actor_create<my_log_t>("log"), но можно явно создавать
объект:
my_log_t log;
lite_actor_name(log.handle(), "log");


ОБРАБОТКА ОШИБОК ---------------------------------------------------------------------------

--- Отправка сообщения об ошибке
lite_error(const char* data, ...)
К введенным данным добавляется в начало "!!! ERROR: " и отправляется сообщением на актор с именем 
"error", который по умолчанию вызывает lite_log(msg->data)

При необходимости можно создать свой актор "error". Он должен быть зарегистрирован до первого
вызова lite_error(). Актор получает нетипизированные сообщения с текстом. msg->data текст.

Особенности регистрации теже что и у "log"


ЗАВЕРШЕНИЕ РАБОТЫ --------------------------------------------------------------------------

--- Ожидание завершения работы всех акторов
lite_thread_end()
Ожидает когда будет полностью завершена работа, удаляет все потоки, акторы и т.д. Возвращает
библиотеку в нулевое состояние. Можно вызывать многократно.


ОСОБЕННОСТИ РАБОТЫ -------------------------------------------------------------------------

Каждый актор имеет очередь входящих сообщений. Отправка сообщения актору это постановка его
в очередь актора и пробуждение простаивающего потока для его обработки.

Поток захвативший актор выполняет его в цикле до тех пор пока очередь сообщений актора не опустеет.

При запуске актора происходит захват ресурса, к которому привязан актор, по окончании работы актора,
ищется ожидающий выполнения актор привязанный к этому же ресурсу.

При обработке последнего сообщения в очереди актора происходит перехват исходящего сообщения актору 
с тем же ресурсом, чтобы по завершению работы с текущим актором в том же потоке запустить получателя.
Другие потоки при этом не оповещаются, поэтому отправку сообщения лучше всего выносить в конец актора. 

Потоки создаются по мере необходимости. Когда все имеющиеся потоки заняты обрабокой акторов, то создается
новый. Потоки нумеруются при создании, при простаивании приоритет пробуждения отдается потоку с меньшим 
номером. Если поток с максимальным номером простаивает 1 секунду - он завершается.


ОТЛАДКА -----------------------------------------------------------------------------------

--- Вывод lite_log() сразу в консоль
#define LT_DEBUG_LOG
Используется встроенный вывод в лог и отключается отложенный вывод, т.к. если происходит прерывание
работы при отложенном выводе (например обращение к несуществующей памяти), то не все последние записи
успевают записаться в лог.

--- Включение счетчиков статистики
#define LT_STAT
Выводятся по окончании lite_thread_end(). Назначение описано ниже в lite_thread_stat_t

--- Вывод в лог информации о состоянии потоков
#define LT_DEBUG
Рекомендуется использовать вместе с LT_DEBUG_LOG, т.к. используется lite_log(), иначе вывод вызывает
дополнительное изменение состояния потоков.

*/

#define LT_VERSION "0.9.0" // Версия библиотеки

#ifndef LT_RESOURCE_DEFAULT
#define LT_RESOURCE_DEFAULT 32 // Предел ресурса по умолчанию
#endif

#ifdef LT_DEBUG
#ifdef NDEBUG
#undef NDEBUG
#endif
#endif

#include <atomic>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <string>
#include <assert.h>
#include <time.h>
#include <string.h>
#include <stdarg.h>

#ifdef LT_STAT
//----------------------------------------------------------------------------------
//------ СЧЕТЧИКИ СТАТИСТИКИ -------------------------------------------------------
//----------------------------------------------------------------------------------
static int64_t lite_time_now();

class lite_thread_stat_t {
	// Глобальные счетчики
	static lite_thread_stat_t& si() noexcept {
		static lite_thread_stat_t x;
		return x;
	}

public:
	size_t stat_thread_max;			// Максимальное количество потоков запущенных одновременно
	size_t stat_parallel_run;		// Максимальное количество потоков работавших одновременно
	size_t stat_thread_create;		// Создано потоков
	size_t stat_thread_wake_up;		// Сколько раз будились потоки
	size_t stat_try_wake_up;		// Попыток разбудить поток
	size_t stat_msg_create;			// Создано сообщений
	size_t stat_msg_erase;			// Удалено сообщений
	size_t stat_actor_get;			// Запросов lite_actor_t* по (func, env)
	size_t stat_actor_find;			// Поиск очередного актора готового к работе
	size_t stat_actor_not_run;		// Промахи обработки сообщения, уже обрабатывается другим потоком
	size_t stat_cache_found;		// Найдено в глобальном кэше
	size_t stat_cache_bad;			// Извлечение из кэша неготового актора
	size_t stat_cache_full;			// Попытка записи в полный кэш
	size_t stat_res_lock;			// Количество блокировок ресурсов
	size_t stat_queue_max;			// Максимальная глубина очереди
	size_t stat_msg_send;			// Обработано сообщений

	// Счетчики потока
	static lite_thread_stat_t& ti() noexcept {
		thread_local lite_thread_stat_t x;
		return x;
	}

	lite_thread_stat_t() {
		init();
		lite_time_now(); // Запуск отсчета времени
	}

	~lite_thread_stat_t() {
		store();
	}

	// Сброс в 0
	void init() {
		memset(this, 0, sizeof(lite_thread_stat_t));
	}

	// Сохранение счетчиков потока в глобальные
	void store() {
		static std::mutex mtx;
		std::unique_lock<std::mutex> lck(mtx); // Блокировка
		if(si().stat_thread_max < stat_thread_max) si().stat_thread_max = stat_thread_max;
		if(si().stat_parallel_run < stat_parallel_run) si().stat_parallel_run = stat_parallel_run;
		si().stat_thread_create += stat_thread_create;
		si().stat_thread_wake_up += stat_thread_wake_up;
		si().stat_try_wake_up += stat_try_wake_up;
		si().stat_msg_create += stat_msg_create;
		si().stat_msg_erase += stat_msg_erase;
		si().stat_actor_get += stat_actor_get;
		si().stat_actor_find += stat_actor_find;
		si().stat_cache_found += stat_cache_found;
		si().stat_cache_bad += stat_cache_bad;
		si().stat_cache_full += stat_cache_full;
		si().stat_res_lock += stat_res_lock;
		si().stat_actor_not_run += stat_actor_not_run;
		if(si().stat_queue_max < stat_queue_max) si().stat_queue_max = stat_queue_max;
		si().stat_msg_send += stat_msg_send;
		init();
	}

	void print_stat() {
		store();
		printf("\n------- STAT -------\n");
		printf("thread_max     %llu\n", (uint64_t)si().stat_thread_max);
		printf("parallel_run   %llu\n", (uint64_t)si().stat_parallel_run);
		printf("thread_create  %llu\n", (uint64_t)si().stat_thread_create);
		printf("thread_wake_up %llu\n", (uint64_t)si().stat_thread_wake_up);
		printf("try_wake_up    %llu\n", (uint64_t)si().stat_try_wake_up);
		printf("msg_create     %llu\n", (uint64_t)si().stat_msg_create);
		printf("actor_get      %llu\n", (uint64_t)si().stat_actor_get);
		printf("actor_find     %llu\n", (uint64_t)si().stat_actor_find);
		printf("actor_not_run  %llu\n", (uint64_t)si().stat_actor_not_run);
		printf("cache_found    %llu\n", (uint64_t)si().stat_cache_found);
		printf("cache_bad      %llu\n", (uint64_t)si().stat_cache_bad);
		printf("cache_full     %llu\n", (uint64_t)si().stat_cache_full);
		printf("resource_lock  %llu\n", (uint64_t)si().stat_res_lock);
		#ifdef LT_STAT_QUEUE
		printf("queue_max      %llu\n", (uint64_t)si().stat_queue_max);
		#endif
		printf("msg_send       %llu\n", (uint64_t)si().stat_msg_send);
		size_t time_ms = lite_time_now();
		printf("msg_send/sec   %llu\n", (uint64_t)si().stat_msg_send * 1000 / (time_ms > 0 ? time_ms : 1)); // Сообщений в секунду
		printf("\n");
		if (si().stat_msg_create != si().stat_msg_erase) printf("!!! ERROR: lost %lld messages\n\n", (int64_t)si().stat_msg_create - si().stat_msg_erase); // Утечка памяти
	}
};

#endif

//----------------------------------------------------------------------------------
//------ БЛОКИРОВКИ ----------------------------------------------------------------
//----------------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#define LOCK_TYPE_LT "critical section"
#include <windows.h>
class lite_mutex_t {
	CRITICAL_SECTION cs;
public:
	lite_mutex_t() {
		//InitializeCriticalSection(&cs);
		InitializeCriticalSectionAndSpinCount(&cs, 1000);
	}

	~lite_mutex_t() {
		DeleteCriticalSection(&cs);
	}

	void lock() noexcept {
		EnterCriticalSection(&cs);
	}

	void unlock() noexcept {
		LeaveCriticalSection(&cs);
	}
};
#else
#define LOCK_TYPE_LT "std::mutex"
typedef std::mutex lite_mutex_t;
#endif

class lite_lock_t {
	lite_mutex_t* mtx;
public:
	lite_lock_t(lite_mutex_t& mtx) noexcept {
		this->mtx = &mtx;
		this->mtx->lock();
	}

	~lite_lock_t() noexcept {
		this->mtx->unlock();
	}
};

// Время с момента запуска, мсек
static int64_t lite_time_now() {
	static std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t);
	return (int64_t)(time_span.count() * 1000);
}

class lite_actor_t;
class lite_thread_t;
class lite_msg_queue_t;
static void lite_error(const char* data, ...) noexcept;
static void lite_log(const char* data, ...) noexcept;
//----------------------------------------------------------------------------------
//-------- СООБЩЕНИE ---------------------------------------------------------------
//----------------------------------------------------------------------------------
#pragma warning( push )
#pragma warning( disable : 4200 ) // MSVC не нравится data[]
// Сообщение
struct lite_msg_t {
	size_t _type;		// Тип сообщения
	size_t _size;		// Размер data, байт

	friend lite_msg_queue_t;
protected:
	lite_msg_t* next;	// Указатель на следующее сообщение в очереди

private:
	// static переменные глобальные ----------------------------------------------------
	typedef std::unordered_map<size_t, std::string> type_name_idx_t;

	struct static_info_t {
		type_name_idx_t tn_idx; // Список типов
		lite_mutex_t mtx;		// Блокировка для доступа к tn_idx
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

public:
	char data[];		// Данные

	lite_msg_t();		// Запрет создания объектов
	lite_msg_t& operator=(const lite_msg_t& m); // Запрет копирования

	// Выделение памяти под сообщение, в случае ошибки возвращает NULL
	static lite_msg_t* create(size_t size, size_t type = 0) noexcept {
		lite_msg_t* msg = (lite_msg_t*)malloc(size + sizeof(lite_msg_t));
		assert(msg != NULL);
		if (msg != NULL) {
			msg->_size = size;
			msg->_type = type;
			msg->next = NULL;
			if (size != 0) msg->data[0] = 0;
		}
		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_msg_create++;
		#endif		
		return msg;
	}

	// Создание типизированного сообщения
	template <typename T>
	static lite_msg_t* create() noexcept {
		return create(sizeof(T), typeid(T).hash_code());
	}

	// Удаление сообщения
	static void erase(lite_msg_t* msg) noexcept {
		if (msg != NULL) {
			#ifdef LT_DEBUG
			assert(msg->next == NULL);
			assert((uint32_t)msg->_type != 0xDDDDDDDD); // Повторное удаление, у MSVC2015 в дебаге free() все переписывает на 0xDD
			msg->_type = 0xDDDDDDDD;
			//used_msg(-1);
			#endif
			free(msg);
			#ifdef LT_STAT
			lite_thread_stat_t::ti().stat_msg_erase++;
			#endif		
		}
	}

	// Указатель на данные
	template <typename T>
	static T* get(lite_msg_t* msg) noexcept {
		if (msg != NULL && sizeof(T) == msg->size()) {
			return (T*)msg->data;
		} else {
			return NULL;
		}
	}

	// Вывод типа сообщения
	template <typename T>
	static size_t type_get() noexcept {
		size_t hash = typeid(T).hash_code();
		lite_lock_t lck(si().mtx); // Блокировка
		// Сохранение названия типа
		if (si().tn_idx.find(hash) == si().tn_idx.end()) {
			si().tn_idx[hash] = typeid(T).name();
		}
		return hash;
	}

	// Тип сообщения строкой
	const std::string type_descr() {
		lite_lock_t lck(si().mtx); // Блокировка
		type_name_idx_t::iterator it = si().tn_idx.find(_type);
		if (it == si().tn_idx.end()) {
			return std::to_string(_type);
		} else {
			return it->second;
		}
	}

	// Тип сообщения
	size_t type() noexcept {
		return _type;
	}

	// Размер сообщения
	size_t size() {
		return _size;
	}
};
#pragma warning( pop )

//----------------------------------------------------------------------------------
//-------- ОЧЕРЕДЬ СООБЩЕНИЙ -------------------------------------------------------
//----------------------------------------------------------------------------------

class lite_msg_queue_t {
	lite_msg_t* msg_first;			// Указатель на первое в очереди
	lite_msg_t* msg_first2;			// Указатель на первое в очереди, меняется только под блокировкой
	lite_msg_t* msg_last;			// Указатель на последнее в очереди
	lite_mutex_t mtx;				// Синхронизация доступа
	#ifdef LT_STAT_QUEUE
	std::atomic<size_t> size;		// Размер очереди
	#endif

public:
	lite_msg_queue_t() : msg_first(NULL), msg_first2(NULL), msg_last(NULL) {
		#ifdef LT_STAT_QUEUE
		size = 0;
		#endif
	}

	// Добавление сообщения в очередь
	void push(lite_msg_t* msg) noexcept {
		msg->next = NULL;
		lite_lock_t lck(mtx); // Блокировка
		if(msg_last == NULL) {
			msg_first2 = msg;
			msg_last = msg;
		} else {
			msg_last->next = msg; // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
			msg_last = msg;
		}
		#ifdef LT_STAT_QUEUE
		size++;
		if (lite_thread_stat_t::ti().stat_queue_max < size) lite_thread_stat_t::ti().stat_queue_max = size;
		#endif
	}

	// Чтение сообщения из очереди. lock = false без блокировки использовать msg_first
	lite_msg_t* pop(bool lock = true) noexcept {
		lock = true;
		if (lock) {
			mtx.lock(); // Блокировка
		}
		if (msg_first == NULL) {
			if (!lock) {
				mtx.lock(); // Блокировка для доступа к msg_first2
				lock = true;
			}
			msg_first = msg_first2;
			msg_first2 = NULL;
		}
		lite_msg_t* msg = msg_first;

		if(msg != NULL) {
			msg_first = msg->next; // Чтение без блокировки
			if (msg_first == NULL) {
				if(!lock) {
					mtx.lock(); // Блокировка для доступа к msg_last
					lock = true;
				}
				msg_first = msg->next; // Повторное чтение под блокировкой на случай если был push
				if(msg_first == NULL) msg_last = NULL;
			}
		}
		if (lock) mtx.unlock(); // Снятие блокировки

		#ifdef LT_DEBUG
		if(msg != NULL) msg->next = NULL;
		#endif
		#ifdef LT_STAT_QUEUE
		if (msg != NULL) size--;
		#endif
		return msg;
	}

	int empty() noexcept {
		return msg_last == NULL;
	}
};

//----------------------------------------------------------------------------------
//------ КЭШ АКТОРОВ ГОТОВЫХ К ВЫПОЛНЕНИЮ ------------------------------------------
//----------------------------------------------------------------------------------
class lite_actor_cache_t {
	std::atomic<lite_actor_t*> next = {0};

public:
	// Запись в кэш
	void push(lite_actor_t* la) noexcept {
		if(next == (lite_actor_t*)NULL) {
			lite_actor_t* nul = NULL;
			if (next.compare_exchange_weak(nul, la)) return;
		}
		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_cache_full++;
		#endif	
	}

	// Извлечение из кэша
	lite_actor_t* pop() noexcept {
		if(next == (lite_actor_t*)NULL) return NULL;
		return next.exchange(NULL);
	}
};

//----------------------------------------------------------------------------------
//------ РЕСУРС --------------------------------------------------------------------
//----------------------------------------------------------------------------------
class alignas(64) lite_resource_t {
	std::atomic<int> res_free; // Свободное количество
	int res_max; // Максимум
	std::string name; // Название ресурса

public:
	lite_actor_cache_t la_cache;

	lite_resource_t() {
		res_free = LT_RESOURCE_DEFAULT;
		res_max = LT_RESOURCE_DEFAULT;
	}

	lite_resource_t(int max) : res_free(max), res_max(max) {
	}
	
	~lite_resource_t() noexcept {
		assert(res_free == res_max);
	}

	// Захват ресурса, возвращает true при успехе
	bool lock() noexcept {
		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_res_lock++;
		#endif
		if(res_free-- <= 0) {
			res_free++;
			return false;
		} else {
			return true;
		}
	}

	// Освобождение ресурса
	void unlock() noexcept {
		res_free++;
	}

	// Количество свободных ресурсов
	bool free() noexcept {
		return res_free > 0;
	}

	void max_set(int max) noexcept {
		if (max <= 0) max = 1;
		if (max == res_max) return;

		res_free += max - res_max;
		res_max = max;
	}

	// Получение имени
	const std::string name_get() {
		return name;
	}

	// static методы глобальные ----------------------------------------------------
	typedef std::unordered_map<std::string, lite_resource_t*> lite_resource_list_t;

	struct static_info_t {
		lite_resource_list_t lr_idx; // Индекс списка ресурсов
		lite_mutex_t mtx;			// Блокировка для доступа к lr_idx
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

	// Создание нового ресурса. max емкость ресурса, при получении указателя на имеющийся можно не указывать
	static lite_resource_t* get(const std::string& name, int max = 0) {
		lite_lock_t lck(si().mtx);
		lite_resource_list_t::iterator it = si().lr_idx.find(name); // Поиск по индексу
		lite_resource_t* lr = NULL;
		if (it != si().lr_idx.end()) {
			lr = it->second;
			if (max != 0) assert(lr->res_max == max);
		} else {
			assert(max != 0);
			lr = new lite_resource_t(max);
			lr->name = name;
			si().lr_idx[name] = lr;
		}
		return lr;
	}

	// Очистка памяти
	static void clear() noexcept {
		lite_lock_t lck(si().mtx);
		for (auto& it : si().lr_idx) {
			delete it.second;
		}
		si().lr_idx.clear();
	}
};

//----------------------------------------------------------------------------------
//------ ОБРАБОТЧИК (АКТОР) --------------------------------------------------------
//----------------------------------------------------------------------------------
// Функция актора
typedef void(*lite_func_t)(lite_msg_t*, void* env);

static size_t lite_thread_num() noexcept;

// Обработчик сообщения (функция + окружение)
class lite_actor_func_t {
public:
	lite_func_t func;
	void* env;

	lite_actor_func_t(lite_func_t func, void* env) : func(func), env(env) { }

	lite_actor_func_t& operator=(const lite_actor_func_t& a) noexcept {
		this->func = a.func;
		this->env = a.env;
	}
};

// Хэш функция и сравнение lite_actor_func_t
namespace std {
	template<> class hash<lite_actor_func_t> {
	public:
		size_t operator()(const lite_actor_func_t &x) const noexcept {
			return (((((size_t)x.env) >> 2) << 16) ^ (((size_t)x.func) >> 4));
		}
	};

	static bool operator==(const lite_actor_func_t& l, const lite_actor_func_t& r) noexcept {
		return r.func == l.func && r.env == l.env;
	}

}

// Актор (функция + очередь сообщений)
class alignas(64) lite_actor_t {

	lite_resource_t* resource;			// Ресурс, используемый актором
	lite_msg_queue_t msg_queue;			// Очередь сообщений
	lite_actor_func_t la_func;			// Функция с окружением
	std::atomic<int> actor_free;		// Количество свободных акторов, т.е. сколько можно запускать
	std::atomic<int> thread_max;		// Количество потоков, в скольки можно одновременно выполнять
	bool in_cache;						// Помещен в кэш планирования запуска
	std::string name;					// Наименование актора

	friend lite_thread_t;
protected:

	//---------------------------------
	// Конструктор
	lite_actor_t(const lite_actor_func_t& la) : la_func(la), actor_free(1), thread_max(1), in_cache(false) {
		resource = &si().res_default;
	}

	// Проверка готовности к запуску
	bool is_ready() noexcept {
		return (!msg_queue.empty() && actor_free > 0 && (resource == NULL || resource == ti().lr_now_used || resource->free()));
	}

	// Постановка сообщения в очередь
	void push(lite_msg_t* msg) noexcept {

		msg_queue.push(msg);

		if (msg == ti().msg_del) {
			// Помеченное на удаление сообщение поместили в очередь другого актора. Снятие пометки
			ti().msg_del = NULL;
		}

		cache_push(this);
	}

	// Запуск обработки всех сообщений очереди
	void run_all() noexcept {
		int free_now = --actor_free;
		if (free_now < 0) {
			// Уже выполняется разрешенное количество акторов
			#ifdef LT_STAT
			lite_thread_stat_t::ti().stat_actor_not_run++;
			#endif
		} else if (resource_lock(resource)) { // Занимаем ресурс
			ti().la_next_run = NULL;
			ti().la_now_run = this;
			bool need_lock = (thread_max != 1); // Блокировка нужна только многопоточным акторам
			while (true) {
				// Извлечение сообщения из очереди
				lite_msg_t* msg = msg_queue.pop(need_lock);
				if (msg == NULL) break;
				// Запуск функции
				ti().msg_del = msg; // Пометка на удаление
				ti().need_wake_up = false;
				la_func.func(msg, la_func.env); // Запуск
				if (msg == ti().msg_del) lite_msg_t::erase(msg);
				#ifdef LT_STAT
				lite_thread_stat_t::ti().stat_msg_send++;
				#endif
			}
			in_cache = false;
		}
		actor_free++;
		return;
	}

public:
	// Получение имени актора
	const std::string& name_get() {
		return name;
	}

private:
	// static переменные уровня потока -------------------------------------------------
	struct thread_info_t {
		lite_msg_t* msg_del;		// Обрабатываемое сообщение, будет удалено после обработки
		bool need_wake_up;			// Необходимо будить другой поток при отправке
		lite_actor_t* la_next_run;	// Следующий на выполнение актор
		lite_actor_t* la_now_run;	// Текущий актор
		lite_resource_t* lr_now_used;// Текущий захваченный ресурс
	};

	static thread_info_t& ti() noexcept {
		thread_local thread_info_t ti = {0};
		return ti;
	}


	// static переменные глобальные ----------------------------------------------------
	typedef std::unordered_map<lite_actor_func_t, lite_actor_t*> lite_actor_idx_t;
	typedef std::unordered_map<std::string, lite_actor_t*> lite_name_idx_t;
	typedef std::vector<lite_actor_t*> lite_actor_list_t;

	struct static_info_t {
		lite_actor_idx_t la_idx;	// Индекс для поиска lite_actor_t* по (func, env)
		lite_name_idx_t la_name_idx;// Индекс для поиска lite_actor_t* по имени
		lite_mutex_t mtx_idx;		// Блокировка для доступа к la_idx. В случае одновременной блокировки сначала mtx_idx затем mtx_list
		lite_actor_list_t la_list; // Список акторов
		lite_mutex_t mtx_list;		// Блокировка для доступа к la_list
		//lite_actor_cache_t la_cache;// Кэш акторов готовых к запуску
		lite_resource_t res_default;// Ресурс по умолчанию
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

	// static методы глобальные ----------------------------------------------------
	// Сохранение в кэш указателя на актор ожидающий исполнения
	void cache_push(lite_actor_t* la) noexcept {
		assert(la != NULL);
		if (!la->is_ready() || la->in_cache) return;

		//if (ti().la_now_run != NULL && ti().la_now_run->msg_queue.empty() && ti().la_next_run == NULL && (ti().lr_now_used == NULL || ti().lr_now_used == la->resource)) {
		if (ti().la_now_run != NULL && ti().la_now_run->msg_queue.empty() && ti().la_next_run == NULL && ti().lr_now_used == la->resource) {
			// Выпоняется последнее задание текущего актора, запоминаем в локальный кэш потока для обработки его следующим
			ti().la_next_run = la;
			return;
		}

		// Запись в кэш ресурса
		la->resource->la_cache.push(la);

		//if (la->resource != NULL) {
		//	// Запись в кэш ресурса
		//	la->resource->la_cache.push(la);
		//} else {
		//	// Помещение в глобальный кэш
		//	si().la_cache.push(la);
		//}
		// Установка флага необходимости пробуждения другого потока
		//if (!ti().need_wake_up) ti().need_wake_up = (la->resource == NULL || la->resource->free());
		if (!ti().need_wake_up) ti().need_wake_up = la->resource->free();
	}

	// Получение из кэша указателя на актор ожидающий исполнения
	static lite_actor_t* cache_pop() noexcept {
		// Проверка локального кэша
		lite_actor_t* la = ti().la_next_run;
		if (la != NULL) {
			ti().la_next_run = NULL;
			if (la->is_ready()) {
				return la;
			} else {
				#ifdef LT_STAT
				lite_thread_stat_t::ti().stat_cache_bad++;
				#endif			
				la = NULL;
			}
		}

		if(ti().lr_now_used != NULL) {
			// Проверка кэша используемого ресурса
			while (la = ti().lr_now_used->la_cache.pop()) {
				if (la->is_ready()) {
					return la;
				}
			}
		}
		//// Проверка кэша используемого ресурса
		//if (ti().lr_now_used != NULL) {
		//	while(la = ti().lr_now_used->la_cache.pop()) {
		//		if (la->is_ready()) {
		//			return la;
		//		}
		//	}
		//}

		//// Проверка глобального кэша
		//while (la = si().la_cache.pop()) {
		//	if (la->is_ready()) {
		//		return la;
		//	}
		//}

		return NULL;
	}

	// Поиск ожидающего выполнение
	static lite_actor_t* find_ready() noexcept {
		// Извлечение из кэша
		lite_actor_t* ret = cache_pop();
		if (ret != NULL) {
			#ifdef LT_STAT
			lite_thread_stat_t::ti().stat_cache_found++;
			#endif
			return ret;
		}

		// Поиск перебором списка акторов
		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_actor_find++;
		#endif

		lite_lock_t lck(si().mtx_list); // Блокировка
		for (lite_actor_list_t::iterator it = si().la_list.begin(); it != si().la_list.end(); it++) {
			if ((*it)->is_ready()) {
				ret = (*it);
				ret->in_cache = true;
				if(it != si().la_list.begin()) {
					// Сдвиг активных ближе к началу
					lite_actor_list_t::iterator it2 = it;
					it2--;
					(*it) = (*it2);
					(*it2) = ret;
				}
				break;
			} 
		}
		return ret;
	}

	// Количество готовых к выполнению
	static size_t count_ready() noexcept {
		lite_lock_t lck(si().mtx_list); // Блокировка
		size_t cnt = 0;
		for (lite_actor_list_t::iterator it = si().la_list.begin(); it != si().la_list.end(); it++) {
			if ((*it)->is_ready()) {
				cnt += (*it)->actor_free;
			}
		}
		return cnt;
	}

	// Захват и освобождение ресурса
	static bool resource_lock(lite_resource_t* res) {
		// Проверка что уже захвачен
		if (res == ti().lr_now_used) return true;
		// Освобождение ранее захваченного
		if (ti().lr_now_used != NULL) ti().lr_now_used->unlock();
		ti().lr_now_used = NULL;
		// Захват нового
		if(res == NULL || res->lock()) {
			ti().lr_now_used = res;
			return true;
		} else {
			return false;
		}
	}

	static bool need_wake_up() {
		return ti().need_wake_up;
	}

	// Очистка всего
	static void clear() noexcept {
		lite_lock_t lck(si().mtx_idx); // Блокировка
		lite_lock_t lck2(si().mtx_list); // Блокировка

		for (auto& a : si().la_list) {
			delete a;
		}
		si().la_idx.clear();
		si().la_name_idx.clear();
		si().la_list.clear();
		//si().la_cache.pop();
	}

public: //-------------------------------------------------------------
	// Указатель на объект связанный с актором
	static lite_actor_t* get(lite_func_t func, void* env = NULL) noexcept {
		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_actor_get++;
		#endif
		lite_actor_func_t a(func, env);

		lite_lock_t lck(si().mtx_idx); // Блокировка
		lite_actor_idx_t::iterator it = si().la_idx.find(a); // Поиск по индексу
		lite_actor_t* la;
		if (it != si().la_idx.end()) {
			la = it->second;
		} else {
			// Добавление
			la = new lite_actor_t(a);
			si().la_idx[a] = la;
			lite_lock_t lck2(si().mtx_list); // Блокировка
			si().la_list.push_back(la);
		}
		return la;
	}

	// Установка имени актора
	static void name_set(lite_actor_t* la, const std::string& name) {
		lite_lock_t lck(si().mtx_idx); // Блокировка
		lite_name_idx_t::iterator it = si().la_name_idx.find(name); // Поиск по индексу
		if (it != si().la_name_idx.end()) {
			if(it->second != la) lite_error("Actor '%s' already exists", name.c_str());
		} else if(!la->name.empty()) {
			lite_error("Try set name '%s' to actor '%s'", name.c_str(), la->name.c_str());
		} else {
			si().la_name_idx[name] = la;
			la->name = name;
		}
	}

	// Получание актора по имени
	static lite_actor_t* name_find(const std::string& name) {
		lite_lock_t lck(si().mtx_idx); // Блокировка
		lite_name_idx_t::iterator it = si().la_name_idx.find(name); // Поиск по индексу
		lite_actor_t* la = NULL;
		if (it != si().la_name_idx.end()) {
			la = it->second;
		}
		return la;
	}

	// Установка глубины распараллеливания
	static void parallel(int count, lite_actor_t* la) noexcept {
		if (count <= 0) count = 1;
		if (count == la->thread_max) return;

		la->actor_free += count - la->thread_max.exchange(count);
	}

	// Копирование сообщения
	static lite_msg_t* msg_copy(lite_msg_t* msg) noexcept {
		if (ti().msg_del == msg) {
			ti().msg_del = NULL; // Снятие пометки на удаление
			return msg;
		} else {
			lite_msg_t* msg2 = lite_msg_t::create(msg->size(), msg->type());
			memcpy(msg2, msg, msg->size());
			return msg2;
		}
	}

	// Установка сообщения об окончании работы
	void resource_set(lite_resource_t* res) noexcept {
		assert(res != NULL);
		if(resource != &si().res_default) {
			lite_error("Actor %s already used resource '%s'", name.c_str(), res->name_get().c_str());
		} else {
			resource = res;
		}
	}

	// Установка максимума ресурсу по умолчанию
	static void resource_max(int max) noexcept {
		si().res_default.max_set(max);
	}

};

//----------------------------------------------------------------------------------
//----- КЛАСС-ОБЕРТКА ДЛЯ АКТОРОВ --------------------------------------------------
//----------------------------------------------------------------------------------
class lite_worker_t {
	std::vector<size_t> type_list;	// Список обрабатываемых типов
	bool type_control = false;		// Задан список принимаетых типов
	lite_actor_t* i_am;
	// Прием входящего сообщения
	static void recv(lite_msg_t* msg, void* env) noexcept {
		lite_worker_t* w = (lite_worker_t*)env;
		if(w->type_control) {
			// Проверка что тип сообщения в обрабатываемых
			bool found = false;
			for(auto& t : w->type_list) {
				if (msg->type() == t) {
					found = true;
					break;
				}
			}
			if(!found) {
				lite_error("%s recv msg type %s", w->handle()->name_get().c_str(), msg->type_descr().c_str());
				return;
			}
		}
		// Обработка сообщения дочерним классом
		w->recv(msg);
	}

public:
	lite_worker_t() {
		i_am = lite_actor_t::get(recv, this);
	}

	// Указатель на актор данного объекта
	lite_actor_t* handle() noexcept {
		return i_am;
	}

	// Добавление обрабатываемого типа
	void type_add(size_t type) noexcept {
		type_control = true;
		bool found = false;
		for (auto& t : type_list) {
			if (type == t) {
				found = true;
				break;
			}
		}
		if (!found) type_list.push_back(type);
	}

	// Обработчик сообщения, прописывать в дочернем классе
	virtual void recv(lite_msg_t* msg) {
		lite_error("method %s work() is not implemented", typeid(this).name());
	}

	virtual ~lite_worker_t() {}

private:
	// static переменные глобальные ----------------------------------------------------
	typedef std::vector<lite_worker_t*> lite_worker_list_t;

	struct static_info_t {
		lite_worker_list_t la_list; // Список оберток
		lite_mutex_t mtx_list;		// Блокировка для доступа к la_list
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

public: 
	// static методы -------------------------------------------------------------
	// Создание именованного объекта
	template <typename T>
	static lite_actor_t* create(const std::string& name) {
		// Акторы log и error можно создавать только явно, т.к. они должны удаляться после всех, чтобы обработать записи из деструкторов
		if(name == "log") {
			lite_error("Actor 'log' can`t be created lite_worker_t::create()");
			return NULL;
		}
		if (name == "error") {
			lite_error("Actor 'log' can`t be created lite_worker_t::create()");
			return NULL;
		}

		lite_worker_t* w = (lite_worker_t*)new T;
		lite_lock_t lck(si().mtx_list); // Блокировка
		si().la_list.push_back(w);
		lite_actor_t* la = w->handle();
		lite_actor_t::name_set(la, name);
		return la;
	}
	
	// Создание безымянного объекта
	//template <typename T>
	//static lite_actor_t* create() {
	//	static std::atomic<size_t> cnt = 0;
	//	std::string name = typeid(T).name();
	//	name += "#";
	//	name += std::to_string(cnt++);
	//	return create<T>(name);
	//}


friend lite_thread_t;
protected:
	// Удаление всех объектов
	static void clear() {
		lite_lock_t lck(si().mtx_list); // Блокировка
		for (auto& w : si().la_list) delete w;
		si().la_list.clear();
	}
};

//----------------------------------------------------------------------------------
//----- ПОТОКИ ---------------------------------------------------------------------
//----------------------------------------------------------------------------------

class alignas(64) lite_thread_t {
	size_t num;					// Номер потока
	std::mutex mtx_sleep;		// Для засыпания
	std::condition_variable cv;	// Для засыпания
	bool is_free;				// Поток свободен
	bool is_end;				// Поток завершен

	// Конструктор
	lite_thread_t(size_t num) : num(num), is_free(true), is_end(false) { }

	// Общие данные всех потоков
	struct static_info_t {
		alignas(64) std::atomic<lite_thread_t*> worker_free = {0}; // Указатель на свободный поток
		std::vector<lite_thread_t*> worker_list;	// Массив описателей потоков
		std::atomic<size_t> thread_count;			// Количество запущеных потоков
		lite_mutex_t mtx;							// Блокировка доступа к массиву потоков
		std::atomic<bool> stop = {0};				// Флаг остановки всех потоков
		std::mutex mtx_end;							// Для ожидания завершения потоков
		std::condition_variable cv_end;				// Для ожидания завершения потоков
	};

	static static_info_t& si() {
		static static_info_t x;
		return x;
	}

	// Создание потока
	static void create_thread() noexcept {
		if (si().stop) return;
		lite_thread_t* lt;
		{
			lite_lock_t lck(si().mtx); // Блокировка
			size_t num = si().thread_count;
			if (si().worker_list.size() == num) {
				si().worker_list.push_back(NULL);
			} else {
				assert(num < si().worker_list.size());
				assert(si().worker_list[num] != NULL);
				assert(si().worker_list[num]->is_end);
				delete si().worker_list[num];
			}
			lt = new lite_thread_t(num);
			si().worker_list[num] = lt;
			si().thread_count++;
		}
		std::thread th(thread_func, lt);
		th.detach();

		#ifdef LT_STAT
		lite_thread_stat_t::ti().stat_thread_create++;
		size_t cnt = si().thread_count;
		if (lite_thread_stat_t::ti().stat_thread_max < cnt) lite_thread_stat_t::ti().stat_thread_max = cnt;
		#endif
	}

	// Поиск свободного потока
	static lite_thread_t* find_free() noexcept {
		lite_thread_t* wf = si().worker_free;
		if(wf != NULL && wf->is_free) return wf;

		if (si().thread_count == 0) return NULL;
		wf = NULL;
		lite_lock_t lck(si().mtx); // Блокировка
		size_t max = si().thread_count;
		assert(max <= si().worker_list.size());

		for (size_t i = 0; i < max; i++) {
			lite_thread_t* w = si().worker_list[i];
			assert(w != NULL);
			if (w->is_free) {
				wf = w;
				break;
			}
		}

		si().worker_free = wf;
		return wf;
	}

	// Подсчет работающих потоков
	static size_t thread_work() noexcept {
		lite_lock_t lck(si().mtx); // Блокировка
		size_t max = si().thread_count;
		assert(max <= si().worker_list.size());

		size_t ret = 0;
		for (size_t i = 0; i < max; i++) {
			if (!si().worker_list[i]->is_free) ret++;
		}
		#ifdef LT_STAT
		if (lite_thread_stat_t::ti().stat_parallel_run < ret) lite_thread_stat_t::ti().stat_parallel_run = ret;
		#endif		
		return ret;
	}

	// Пробуждение свободного потока
	static void wake_up() noexcept {
		lite_thread_t* wf = find_free();
		if(wf != NULL) {
			wf->cv.notify_one();
			#ifdef LT_STAT
			lite_thread_stat_t::ti().stat_try_wake_up++;
			#endif
		} else {
			create_thread();
		}
	}

	// Обработка сообщений
	static void work_msg(lite_actor_t* la = NULL) noexcept {
		if (la == NULL) la = lite_actor_t::find_ready();
		while (la != NULL) {
			la->run_all();
			la = lite_actor_t::find_ready();
		}
		lite_actor_t::resource_lock(NULL);
	}

	// Функция потока
	static void thread_func(lite_thread_t* const lt) noexcept {
		#ifdef LT_DEBUG
		lite_log("thread#%d start", (int)lt->num);
		#endif
		this_num(lt->num);
		// Пробуждение другого потока если ожидающих акторов больше одного
		if(lite_actor_t::count_ready() > 1) {
			lt->is_free = false;
			wake_up();
		}
		// Цикл обработки сообщений
		while(true) {
			// Проверка необходимости и создание новых потоков
			lite_actor_t* la = lite_actor_t::find_ready();
			if(la != NULL) { // Есть что обрабатывать
				lt->is_free = false;
				// Обработка сообщений
				work_msg(la);
				lt->is_free = true;
			}
			if (si().stop) break;
			// Уход в ожидание
			bool stop = false;
			{
				#ifdef LT_DEBUG
				lite_log("thread#%d sleep", (int)lt->num);
				#endif
				if(thread_work() == 0) si().cv_end.notify_one(); // Если никто не работает, то разбудить ожидание завершения
				lite_thread_t* wf = si().worker_free;
				while(wf == NULL || wf->num > lt->num) { // Следующим будить поток с меньшим номером
					si().worker_free.compare_exchange_weak(wf, lt);
				}
				std::unique_lock<std::mutex> lck(lt->mtx_sleep);
				lt->is_free = true;
				if(lt->cv.wait_for(lck, std::chrono::seconds(1)) == std::cv_status::timeout) {	// Проснулся по таймауту
					stop = (lt->num == si().thread_count - 1);	// Остановка потока с наибольшим номером
					#ifdef LT_DEBUG
					lite_log("thread#%d wake up (total: %d, work: %d)", (int)lt->num, (int)si().thread_count, (int)thread_work());
					#endif
				} else {
					#ifdef LT_DEBUG
					lite_log("thread#%d wake up", (int)lt->num);
					#endif
					#ifdef LT_STAT
					lite_thread_stat_t::ti().stat_thread_wake_up++;
					#endif
				}
				//if (si().worker_free == lt) si().worker_free = NULL;
				if (si().worker_free == lt) {
					wf = lt;
					si().worker_free.compare_exchange_weak(wf, NULL);
				}
			}
			if (stop) {
				lt->is_free = false;
				break;
			}
		}
		lite_lock_t lck(si().mtx); // Блокировка
		lt->is_end = true;
		lt->is_free = false;
		si().thread_count--;
		si().cv_end.notify_one(); // пробуждение end()
		#ifdef LT_DEBUG
		lite_log("thread#%d stop", (int)lt->num);
		#endif

	}

public: //-------------------------------------
	// Помещение в очередь для последующего запуска
	static void run(lite_msg_t* msg, lite_actor_t* la) noexcept {
		la->push(msg);
		if (lite_actor_t::need_wake_up() || si().thread_count == 0) wake_up();
	}

	// Завершение, ожидание всех потоков
	static void end() noexcept {
		// Рассчет еще не начался
		if(si().thread_count == 1 && thread_work() == 0) {
			#ifdef LT_DEBUG
			lite_log("--- wait start ---");
			#endif	
			std::this_thread::sleep_for(std::chrono::milliseconds(100)); // для запуска потоков
		}
		#ifdef LT_DEBUG
		lite_log("--- wait all ---");
		#endif	
		// Ожидание завершения расчетов. 
		while(thread_work() > 0) {
			std::unique_lock<std::mutex> lck(si().mtx_end);
			si().cv_end.wait_for(lck, std::chrono::milliseconds(300));
		}
		#ifdef LT_DEBUG
		lite_log("--- stop all ---");
		#endif	
		// Остановка потоков
		si().stop = true;
		while(true) { // Ожидание остановки всех потоков
			bool is_end = true;
			{
				lite_lock_t lck(si().mtx); // Блокировка
				for (auto& w : si().worker_list) {
					if(!w->is_end) { // Поток не завершился
						w->cv.notify_one(); // Пробуждение потока
						is_end = false;
					}
				}
			}
			if (is_end) {
				break;
			} else { // Ожидание пока какой-нибудь завершившийся поток не разбудит
				std::unique_lock<std::mutex> lck(si().mtx_end);
				si().cv_end.wait_for(lck, std::chrono::milliseconds(100));
			}
		}
		// Завершены все потоки
		assert(si().thread_count == 0);

		// Очистка данных потоков
		lite_lock_t lck(si().mtx); // Блокировка
		for (auto& w : si().worker_list) {
			assert(w != NULL);
			delete w;
			w = NULL;
		}
		si().worker_list.clear();
		si().worker_free = NULL;
		// Дообработка необработанных сообщений. 
		work_msg();
		// Очистка памяти под акторы-объекты
		lite_worker_t::clear();
		// Дообработка сообщений из деструкторов объектов
		work_msg();
		// Очистка памяти под акторы
		lite_actor_t::clear();
		// Очистка памяти под ресурсы
		lite_resource_t::clear();
		#ifdef LT_STAT
		lite_thread_stat_t::ti().print_stat();
		#endif		
		#ifdef LT_DEBUG
		printf("         !!! end !!!\n");
		#endif
		si().stop = false;
	}

	// Номер текущего потока
	static size_t this_num(size_t num = 99999) noexcept {
		thread_local size_t n = 99999;
		if (num != 99999) {
			n = num;
		}
		return n;
	}
};

//----------------------------------------------------------------------------------
//----- ВЫВОД В ЛОГ ----------------------------------------------------------------
//----------------------------------------------------------------------------------
#ifndef LITE_LOG_BUF_SIZE
#define LITE_LOG_BUF_SIZE 1024
#endif

// Вывод по умолчанию в консоль. При необходимости зарегистрировать свой актор "log"
static void lite_log_write(lite_msg_t* msg, void* env) {
	msg->data[msg->size() - 1] = 0;
	printf("%s\n", msg->data + 9);
}

// Запись в лог
static void lite_log(const char* data, ...) noexcept {
	va_list ap;
	va_start(ap, data);
	lite_msg_t* msg = lite_msg_t::create(LITE_LOG_BUF_SIZE);

	time_t rawtime;
	time(&rawtime);

	size_t size = 0;
	char* p = msg->data;

#ifdef WIN32
	struct tm timeinfo;
	localtime_s(&timeinfo, &rawtime);
	size += sprintf_s(p, LITE_LOG_BUF_SIZE - size, "%02d.%02d.%02d %02d:%02d:%02d ", timeinfo.tm_mday, timeinfo.tm_mon, timeinfo.tm_year % 100, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	p += size;
	size += vsprintf_s(p, LITE_LOG_BUF_SIZE - size, data, ap);
	p += size;
#else
	size += snprintf(p, LITE_LOG_BUF_SIZE - size, "%02d.%02d.%02d %02d:%02d:%02d ", timeinfo->tm_mday, timeinfo->tm_mon, timeinfo->tm_year % 100, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	p += size;
	size += vsnprintf(p, LITE_LOG_BUF_SIZE - size, data, ap);
	p += size;
#endif
	va_end(ap);
	assert(size < LITE_LOG_BUF_SIZE);
	*p = 0;
	// Отправка на вывод
	lite_actor_t* log = lite_actor_t::name_find("log");
	if(log == NULL) { // Нет актора "log"
		// Регистрация lite_log_write()
		log = lite_actor_t::get(lite_log_write, NULL);
		lite_actor_t::name_set(log, "log");
	}
	#ifdef LT_DEBUG_LOG
	lite_log_write(msg, NULL);
	lite_msg_t::erase(msg);
	#else
	lite_thread_t::run(msg, log);
	#endif
}

//----------------------------------------------------------------------------------
//----- ЗАПИСЬ ОШИБКИ В ЛОГ --------------------------------------------------------
//----------------------------------------------------------------------------------

// Вывод по умолчанию в лог. При необходимости зарегистрировать свой актор "error"
static void lite_error_write(lite_msg_t* msg, void* env) {
	msg->data[msg->size() - 1] = 0;
	lite_log(msg->data);
}

// Запись ошибки
static void lite_error(const char* data, ...) noexcept {
	va_list ap;
	va_start(ap, data);
	lite_msg_t* msg = lite_msg_t::create(LITE_LOG_BUF_SIZE);

	size_t size = 0;
	char* p = msg->data;

	#ifdef WIN32
	size += sprintf_s(p, LITE_LOG_BUF_SIZE - size, "!!! ERROR: ");
	p += size;
	size += vsprintf_s(p, LITE_LOG_BUF_SIZE - size, data, ap);
	p += size;
	#else
	struct tm * timeinfo = localtime(&rawtime);
	size += snprintf(p, LITE_LOG_BUF_SIZE - size, "!!! ERROR: ");
	p += size;
	size += vsnprintf(p, LITE_LOG_BUF_SIZE - size, data, ap);
	p += size;
	#endif
	va_end(ap);
	assert(size < LITE_LOG_BUF_SIZE);
	*p = 0;
	// Отправка на вывод
	lite_actor_t* error = lite_actor_t::name_find("error");
	if (error == NULL) { // Нет актора "error"
		// Регистрация lite_log_write()
		error = lite_actor_t::get(lite_error_write, NULL);
		lite_actor_t::name_set(error, "error");
	}
	#ifdef LT_DEBUG_LOG
	lite_error_write(msg, NULL);
	lite_msg_t::erase(msg);
	#else
	lite_thread_t::run(msg, error);
	#endif
}

//----------------------------------------------------------------------------------
//----- ОБЕРТКИ --------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Выделение памяти под сообщение, в случае ошибки возвращает NULL
static lite_msg_t* lite_msg_create(size_t size, size_t type = 0) noexcept {
	return lite_msg_t::create(size, type);
}

// Выделение памяти под типизированное сообщение, в случае ошибки возвращает NULL
template <typename T>
static lite_msg_t* lite_msg_create() noexcept {
	return lite_msg_t::create<T>();
}

// Указатель на данные сообщения, NULL если тип не совпадает
template <typename T>
static T* lite_msg_data(lite_msg_t* msg) noexcept {
	return lite_msg_t::get<T>(msg);
}

// Получение типа сообщения
template <typename T>
static size_t lite_msg_type() {
	return lite_msg_t::type_get<T>();
}

// Удаление сообщения
static void lite_msg_erase(lite_msg_t* msg) noexcept {
	lite_msg_t::erase(msg);
}

// Копирование сообщения
static lite_msg_t* lite_msg_copy(lite_msg_t* msg) noexcept {
	return lite_actor_t::msg_copy(msg);
}

// Получения указателя на актор по функции
static lite_actor_t* lite_actor_get(lite_func_t func, void* env = NULL) noexcept {
	return lite_actor_t::get(func, env);
}

// Получения указателя на актор по имени
static lite_actor_t* lite_actor_get(const std::string& name) noexcept {
	return lite_actor_t::name_find(name);
}

// Создание актора из объекта унаследованного от lite_worker_t
template <typename T>
static lite_actor_t* lite_actor_create() noexcept {
	static std::atomic<size_t> cnt = {0};
	std::string name = typeid(T).name();
	name += "#";
	name += std::to_string(cnt++);
	return lite_worker_t::create<T>(name);
}

// Создание именованного актора из объекта унаследованного от lite_worker_t
template <typename T>
static lite_actor_t* lite_actor_create(const std::string& name) noexcept {
	return lite_worker_t::create<T>(name);
}

// Установка имени актору
static void lite_actor_name(lite_actor_t* la, const std::string& name) noexcept {
	lite_actor_t::name_set(la, name);
}

// Установка глубины распараллеливания
static void lite_actor_parallel(int max, lite_actor_t* la) noexcept {
	lite_actor_t::parallel(max, la);
}

static void lite_actor_parallel(int max, lite_func_t func, void* env = NULL) noexcept {
	lite_actor_t::parallel(max, lite_actor_t::get(func, env));
}

// Отправка на выполнение по указателю на актор
static void lite_thread_run(lite_msg_t* msg, lite_actor_t* la) noexcept {
	lite_thread_t::run(msg, la);
}

static void lite_thread_run(lite_msg_t* msg, lite_func_t func, void* env = NULL) noexcept {
	lite_thread_t::run(msg, lite_actor_t::get(func, env));
}

// Завершение с ожиданием всех
static void lite_thread_end() noexcept {
	lite_thread_t::end();
}

// Номер текущего потока
static size_t lite_thread_num() noexcept {
	return lite_thread_t::this_num();
}

// Установка максимума ресурсу по умолчанию
static void lite_thread_max(int max) noexcept {
	return lite_actor_t::resource_max(max);
}

// Создание ресурса
static lite_resource_t* lite_resource_create(const std::string& name, int max) noexcept {
	return lite_resource_t::get(name, max);
}

// Привязка актора к ресурсу
static void lite_resource_set(const std::string& name, lite_actor_t* la) noexcept {
	la->resource_set(lite_resource_t::get(name));
}

static void lite_resource_set(const std::string& name, lite_func_t func, void* env = NULL) noexcept {
	lite_actor_t::get(func, env)->resource_set(lite_resource_t::get(name));
}
