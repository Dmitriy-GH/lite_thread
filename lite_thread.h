#pragma once
/* lite_thread - библиотека для легкого распараллеливания по Модели Акторов
   библиотека создает требуемое количество потоков и распределяет по ним вызовы акторов.

   Актор это функция (статический метод класса) и его окружение (func+env). Тип функции:

   void func(lite_msg_t* msg, void* env)
   
   Для объектов создать static метод с этой сигнатурой и в качестве env передавать this

   Каждая пара func+env сохраняется в один актор lite_actor_t, расположение актора в памяти постоянно, 
   поэтому для ускорения работы в коде лучше использовать указатель, т.к. иначе каждый раз происходит
   поиск указателя в индексе. Для получения указателя вызвать:
   lite_actor_t* lite_actor_get(lite_func_t func, void* env = NULL)

   func() может быть вызвана в любом потоке, но для каждой пары func(env) гарантируется что они не будут 
   вызваны одновременно в нескольких потоках. За исключением случаев явного указания в скольки потоках 
   можно вызывать func(env). Для указания глубины распараллеливания вызвать:
   lite_actor_parallel(int count, lite_actor_t* la)
   lite_actor_parallel(int count, lite_func_t func, void* env = NULL)

   Взаимодействие между акторами идет через сообщения. Для создания сообщения использовать
   lite_msg_t* lite_msg_create(size_t size, int type = 0) // size размер в байтах
   lite_msg_t* lite_msg_create<T>(int type = 0) // size = sizeof(T)

   Для приведения указателей на содержимое сообщения к нужному типу
   T* lite_msg_data<T>(lite_msg_t* msg) // Вернет NULL при несовпадении размера сообщения и sizeof(T)

   type можно использовать для внутренней идентификации. Библиотека никак его не использует.

   Для передачи сообщения на обработку:
   lite_thread_run(lite_msg_t* msg, lite_actor_t* la)
   lite_thread_run(lite_msg_t* msg, lite_func_t func, void* env = NULL)

   ВАЖНО: Сообщения НЕ иммутабельны, поэтому:
   - нельзя отправлять одно сообщение дважды;
   - нельзя читать/писать сообщение после отправки, т.к. оно может быть уже в обработке или удалено.

   Сообщение можно изменять и отправлять дальше.
   
   Гарантируется что одно сообщение в любой момент времени обрабатывается только в одном потоке.
   
   Гарантируется что каждый обработчик получит сообщения в том порядке, в котором они отправлены.

   Сообщения удаляются автоматически после обработки (кроме случаев если была отправка этого сообщения), 
   поэтому созданное, но неотправленное сообщение надо удалять самостоятельно иначе будет утечка памяти.
   Для использования сообщения в собственных целях его надо скопировать с помощью lite_msg_copy() и по 
   окончании удалить lite_msg_erase()

   Для оповещения о прекращении работы можно зарегистрировать соответствующее сообщение. По окончанию 
   оно будет отправлено. При обработке этого сообщения запрещено отправлять исходящие сообщения другим.
   Для регистрации сообщения о прекращении работы вызвать:
   lite_msg_end(lite_msg_t* msg, lite_actor_t* la)
   lite_msg_end(lite_msg_t* msg, lite_func_t func, void* env = NULL)

   Для ожидания завершения все расчетов вызвать:
   lite_thread_end()

   PS Функции с идентичным кодом оптимизатор компилятора может преврашать в одну, как следствие вызов 
   обеих пойдет в одной очереди.
*/

#include <atomic>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <assert.h>
#include <time.h>
#include <shared_mutex>

#ifdef _DEBUG
#ifndef DEBUG_LT
#define DEBUG_LT
#endif
#endif

#ifdef STAT_LT
// Счетчики для отладки
std::atomic<uint32_t> stat_thread_max;		// Максимальное количество потоков запущенных одновременно
std::atomic<uint32_t> stat_parallel_run;	// Максимальное количество потоков работавших параллельно
std::atomic<uint32_t> stat_thread_create;	// Создано потоков
std::atomic<uint32_t> stat_thread_wake_up;	// Сколько раз будились потоки
std::atomic<uint32_t> stat_msg_create;		// Создано сообщений
std::atomic<uint32_t> stat_actor_get;		// Запросов lite_actor_t* по (func, env)
std::atomic<uint32_t> stat_actor_find;		// Поиск очередного актора готового к работе
std::atomic<uint32_t> stat_cache_bad;		// Извлечение из кэша неготового актора
std::atomic<uint32_t> stat_cache_full;		// Попытка записи в полный кэш
std::atomic<uint32_t> stat_msg_not_run;		// Промахи обработки сообщения, уже обрабатывается другим потоком
std::atomic<uint32_t> stat_queue_max;		// Максимальная глубина очереди
std::atomic<uint32_t> stat_queue_push;		// Счетчик помещения сообщений в очередь
std::atomic<uint32_t> stat_msg_run;			// Обработано сообщений

void print_stat() {
	printf("\n------- STAT -------\n");
	printf("thread_max     %u\n", (uint32_t)stat_thread_max);
	printf("parallel_run   %u\n", (uint32_t)stat_parallel_run);
	printf("thread_create  %u\n", (uint32_t)stat_thread_create);
	printf("thread_wake_up %u\n", (uint32_t)stat_thread_wake_up);
	printf("msg_create     %u\n", (uint32_t)stat_msg_create);
	printf("actor_get      %u\n", (uint32_t)stat_actor_get);
	printf("actor_find     %u\n", (uint32_t)stat_actor_find);
	printf("cache_bad      %u\n", (uint32_t)stat_cache_bad);
	printf("cache_full     %u\n", (uint32_t)stat_cache_full);
	printf("msg_not_run    %u\n", (uint32_t)stat_msg_not_run);
	printf("queue_max      %u\n", (uint32_t)stat_queue_max);
	printf("queue_push     %u\n", (uint32_t)stat_queue_push);
	printf("msg_run        %u\n", (uint32_t)stat_msg_run);
}
#endif

//----------------------------------------------------------------------------------
//------ ОБЩЕЕ ---------------------------------------------------------------------
//----------------------------------------------------------------------------------
#if defined(_WIN32) || defined(_WIN64)
#define LOCK_TYPE_LT "critical section"
#include <windows.h>
class lite_mutex_t {
	CRITICAL_SECTION cs;
public:
	lite_mutex_t() {
		InitializeCriticalSection(&cs);
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

//----------------------------------------------------------------------------------
//-------- СООБЩЕНИE ---------------------------------------------------------------
//----------------------------------------------------------------------------------
class lite_actor_t;
class lite_thread_t;
#pragma warning( push )
#pragma warning( disable : 4200 ) // MSVC не нравится data[]
// Сообщение
struct lite_msg_t {
	int type;			// Тип сообщения
	size_t size;		// Размер data, байт
	char data[];		// Данные

	lite_msg_t();		// Запрет создания объектов
	lite_msg_t& operator=(const lite_msg_t& m); // Запрет копирования

	// Счетчик используемых сообщений, меняется только при _DEBUG
	static int used_msg(int delta = 0) noexcept {
		static std::atomic<int> used = {0};
		used += delta;
		return used;
	}

	// Выделение памяти под сообщение, в случае ошибки возвращает NULL
	static lite_msg_t* create(size_t size, int type = 0) noexcept {
		lite_msg_t* msg = (lite_msg_t*)malloc(size + sizeof(lite_msg_t));
		assert(msg != NULL);
		if (msg != NULL) {
			msg->size = size;
			msg->type = type;
			if (size != 0) msg->data[0] = 0;
		}
		#ifdef DEBUG_LT
		used_msg(1);
		#endif
		#ifdef STAT_LT
		stat_msg_create++;
		#endif		
		return msg;
	}

	template <typename T>
	static lite_msg_t* create(uint32_t type = 0) noexcept {
		return create(sizeof(T), type);
	}

	// Удаление сообщения
	static void erase(lite_msg_t* msg) noexcept {
		if (msg != NULL) {
			#ifdef DEBUG_LT
			assert(msg->type != 0xDDDDDDDD); // Повторное удаление, у MSVC2015 в дебаге free() все переписывает на 0xDD
			msg->type = 0xDDDDDDDD;
			used_msg(-1);
			#endif
			free(msg);
		}
	}

	// Указатель на данные
	template <typename T>
	static T* get(lite_msg_t* msg) noexcept {
		if (msg != NULL && sizeof(T) == msg->size) {
			return (T*)msg->data;
		} else {
			return NULL;
		}
	}

};
#pragma warning( pop )

//----------------------------------------------------------------------------------
//-------- ОЧЕРЕДЬ СООБЩЕНИЙ -------------------------------------------------------
//----------------------------------------------------------------------------------

class lite_msg_queue_t {
	std::queue<lite_msg_t*> queue;	// Очередь сообщений
	lite_msg_t* msg_one;			// Альтернатива очереди при msg_count == 1
	lite_mutex_t mtx;				// Синхронизация доступа
	bool is_empty;				// Флаг что очередь не пуста
public:
	lite_msg_queue_t() : msg_one(NULL), is_empty(true) {}

	// Добавление сообщения в очередь, возврашает размер
	void push(lite_msg_t* msg) noexcept {
		lite_lock_t lck(mtx); // Блокировка
		if (msg_one == NULL && queue.size() == 0) {
			// 1-е сообщение. Запись в msg_one
			msg_one = msg;
		} else {
			// 2-е сообщение. Запись в очередь
			queue.push(msg);
			#ifdef STAT_LT
			stat_queue_push++;
			uint32_t now = queue.size() + (msg_one == NULL ? 0 : 1);
			if (stat_queue_max < now) stat_queue_max = now;
			#endif
		}
		is_empty = false;
		//cnt++;
	}

	// Чтение сообщения из очереди
	lite_msg_t* pop() noexcept {
		lite_lock_t lck(mtx); // Блокировка
		// Чтение первого из msg_one
		lite_msg_t* msg = msg_one;
		if (msg != NULL) {
			msg_one = NULL;
			is_empty = (queue.size() == 0);
			return msg;
		}
		// Чтение из очереди
		msg = NULL;
		if (queue.size() != 0) {
			msg = queue.front();
			queue.pop();
		}
		is_empty = (queue.size() == 0);
		return msg;
	}

	int empty() noexcept {
		return is_empty;
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

	lite_msg_queue_t msg_queue;			// Очередь сообщений
	lite_actor_func_t la_func;			// Функция с окружением
	std::atomic<int> actor_free;		// Количество свободных акторов, т.е. сколько можно запускать
	std::atomic<int> thread_max;		// Количество потоков, в скольки можно одновременно выполнять
	std::atomic<lite_msg_t*> msg_end;	// Сообщение об окончании работы
	bool in_cache;						// Помещен в кэш планирования запуска

	friend lite_thread_t;
protected:

	//---------------------------------
	// Конструктор
	lite_actor_t(const lite_actor_func_t& la) : la_func(la), actor_free(1), thread_max(1), msg_end(NULL), in_cache(false) {	}

	// Деструктор
	~lite_actor_t() {
		if (msg_end != (lite_msg_t*)NULL) {
			// Отправка сообщения об окончании работы
			la_func.func(msg_end, la_func.env);
			lite_msg_t::erase(msg_end);
		}
	}

	// Проверка готовности к запуску
	bool is_ready() noexcept {
		return (!msg_queue.empty() && actor_free > 0);
	}

	// Постановка сообщения в очередь, возврашает true если надо будить другой поток
	bool push(lite_msg_t* msg) noexcept {
		//
		msg_queue.push(msg);

		if (msg == ti().msg_del) {
			// Помеченное на удаление сообщение поместили в очередь другого актора. Снятие пометки
			ti().msg_del = NULL;
		} else if (ti().msg_del == NULL) {
			// Не было предыдушего сообщения, отправка из чужого потока
			ti().need_wake_up = true;
		}

		cache_push(this);

		return ti().need_wake_up;
	}

	// Запуск обработки сообщения, если не задано то из очереди
	/*void run() noexcept {
		if (actor_free-- <= 0) {
			// Уже выполняется разрешенное количество акторов
			#ifdef STAT_LT
			stat_msg_not_run++;
			#endif
		} else {
			// Извлечение сообщения из очереди
			lite_msg_t* msg =  msg_queue.pop();

			if (msg != NULL) { // Запуск функции
				#ifdef STAT_LT
				stat_msg_run++;
				#endif
				ti().msg_del = msg; // Пометка на удаление
				ti().need_wake_up = false;
				ti().la_now_run = this;
				la_func.func(msg, la_func.env); // Запуск
				if (msg == ti().msg_del) lite_msg_t::erase(msg);
			} else {
				#ifdef STAT_LT
				stat_msg_not_run++;
				#endif
			}
		}
		actor_free++;
		return;
	}*/

	// Запуск обработки всех сообщений очереди
	void run_all() noexcept {
		int free_now = --actor_free;
		if (free_now < 0) {
			// Уже выполняется разрешенное количество акторов
			#ifdef STAT_LT
			stat_msg_not_run++;
			#endif
		} else {
			#ifdef STAT_LT
			int stat = 0;
			#endif
			ti().la_next_run = NULL;
			ti().la_now_run = this;
			while (true) {
				// Извлечение сообщения из очереди
				lite_msg_t* msg = msg_queue.pop();
				if (msg == NULL) break;
				// Запуск функции
				ti().msg_del = msg; // Пометка на удаление
				ti().need_wake_up = false;
				la_func.func(msg, la_func.env); // Запуск
				if (msg == ti().msg_del) lite_msg_t::erase(msg);
				#ifdef STAT_LT
				stat++;
				#endif
			}
			in_cache = false;
			#ifdef STAT_LT
			stat_msg_run += stat;
			if (stat == 0) stat_msg_not_run++;
			#endif
		}
		actor_free++;
		return;
	}
	// static методы уровня потока -------------------------------------------------
	struct thread_info_t {
		lite_msg_t* msg_del;		// Обрабатываемое сообщение, будет удалено после обработки
		bool need_wake_up;			// Необходимо будить другой поток при отправке
		lite_actor_t* la_next_run;	// Следующий на выполнение актор
		lite_actor_t* la_now_run;	// Текущий актор
	};

	// Текущее сообщение в потоке
	static thread_info_t& ti() noexcept {
		thread_local thread_info_t ti = {0};
		return ti;
	}


	// static методы глобальные ----------------------------------------------------
	typedef std::unordered_map<lite_actor_func_t, lite_actor_t*> lite_actor_list_t;
	typedef std::vector<lite_actor_t*> lite_actor_cache_t;

	struct static_info_t {
		lite_actor_list_t la_idx;	// Индекс для поиска lite_actor_t*
		//lite_mutex_t mtx_idx;		// Блокировка для доступа к la_idx. В случае одновременной блокировки сначала mtx_idx затем mtx_list
		std::shared_mutex mtx_idx;	// Блокировка для доступа к la_idx. В случае одновременной блокировки сначала mtx_idx затем mtx_list
		lite_actor_cache_t la_list; // Кэш списка акторов
		//lite_mutex_t mtx_list;	// Блокировка для доступа к la_list
		std::shared_mutex mtx_list;	// Блокировка для доступа к la_list
		std::atomic<lite_actor_t*> la_next_run;	// Следующий на выполнение актор
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

	// Очистка всего
	static void clear() noexcept {
		//lite_lock_t lck(si().mtx_idx); // Блокировка
		std::unique_lock<std::shared_mutex> lck(si().mtx_idx); // Блокировка
		//lite_lock_t lck2(si().mtx_list); // Блокировка
		std::unique_lock<std::shared_mutex> lck2(si().mtx_list); // Блокировка

		for(auto& a : si().la_list) {
			delete a;
		}
		si().la_idx.clear();
		si().la_list.clear();
	}

	// Сохранение в кэш указателя на актор ожидающий исполнения
	void cache_push(lite_actor_t* la) noexcept {
		assert(la != NULL);
		if (!la->is_ready() || la->in_cache) return;

		if(ti().la_now_run != NULL && ti().la_now_run->msg_queue.empty() && ti().la_next_run == NULL) {
			// Выпоняется последнее задание текущего актора, запоминаем в локальный кэш для обработки его следующим
			ti().la_next_run = la;
			return;
		}

		// Помещение в глобальный кэш
		lite_actor_t* nul = NULL;
		if (si().la_next_run == (lite_actor_t*)NULL && si().la_next_run.compare_exchange_weak(nul, la)) {
			la->in_cache = true;
			return;
		} else {
			// Установка флага пробуждения другого потока
			ti().need_wake_up = true;
			#ifdef STAT_LT
			stat_cache_full++;
			#endif			
		}
	}
/*	void cache_push(lite_actor_t* la, int msg_cnt) noexcept {
		assert(la != NULL);
		if (!la->is_ready()) return;
		if (ti().la_next_run == NULL) { // Пустой кэш потока
			ti().la_next_run = la;
		} else if (si().la_next_run == (lite_actor_t*)NULL) {
			// Пустой глобальный кэш, перемещение из локального в глобальный, а в локальный la
			lite_actor_t* nul = NULL;
			if (si().la_next_run.compare_exchange_weak(nul, ti().la_next_run)) {
				ti().la_next_run = la;
			} else {
				#ifdef STAT_LT
				stat_cache_full++;
				#endif			
			}
			// Установка флага пробуждения другого потока
			ti().need_wake_up = true;
		}
	}*/

	// Получение из кэша указателя на актор ожидающий исполнения
	static lite_actor_t* cache_pop() noexcept {
		// Проверка локального кэша
		lite_actor_t* la = ti().la_next_run;
		if (la != NULL) {
			ti().la_next_run = NULL;
			if (la->is_ready()) {
				return la;
			} else {
				#ifdef STAT_LT
				stat_cache_bad++;
				#endif			
			}
		}
		// Проверка глобального кэша
		la = si().la_next_run.exchange(NULL);
		if (la != NULL && la->is_ready()) {
			return la;
		//#ifdef STAT_LT
		//} else if(la != NULL) {
		//	stat_cache_bad++;
		//#endif			
		}
		return NULL;
	}

	// Поиск ожидающего выполнение
	static lite_actor_t* find_ready() noexcept {
		// Указатель закэшированный  в потоке
		lite_actor_t* ret = cache_pop();
		if (ret != NULL) return ret;

		#ifdef STAT_LT
		stat_actor_find++;
		#endif

		std::shared_lock<std::shared_mutex> lck2(si().mtx_list); // Блокировка
		//lite_lock_t lck(si().mtx_list); // Блокировка
		for (lite_actor_cache_t::iterator it = si().la_list.begin(); it != si().la_list.end(); it++) {
			if ((*it)->is_ready()) {
				ret = (*it);
				//if(it != si().la_list.begin()) {
				//	// Сдвиг активных ближе к началу
				//	lite_actor_cache_t::iterator it2 = it;
				//	it2--;
				//	(*it) = (*it2);
				//	(*it2) = ret;
				//}
				break;
			} 
		}
		return ret;
	}

public: //-------------------------------------------------------------
	// Указатель на объект связанный с актором
	static lite_actor_t* get(lite_func_t func, void* env = NULL) noexcept {
		#ifdef STAT_LT
		stat_actor_get++;
		#endif
		lite_actor_func_t a(func, env);
		lite_actor_t* la = NULL;
		{
			//lite_lock_t lck(si().mtx_idx); // Блокировка
			std::shared_lock<std::shared_mutex> lck(si().mtx_idx); // Блокировка

			lite_actor_list_t::iterator it = si().la_idx.find(a); // Поиск по индексу
			if (it != si().la_idx.end()) {
				la = it->second;
			} 
		}
		if(la == NULL) {
			// Добавление
			std::unique_lock<std::shared_mutex> lck(si().mtx_idx); // Блокировка
			la = new lite_actor_t(a);
			si().la_idx[a] = la;
			//lite_lock_t lck2(si().mtx_list); // Блокировка
			std::unique_lock<std::shared_mutex> lck2(si().mtx_list); // Блокировка
			si().la_list.push_back(la);
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
		if (ti().msg_del == msg) ti().msg_del = NULL; // Снятие пометки на удаление
		return msg;
	}

	// Установка сообщения об окончании работы
	static void actor_end(lite_msg_t* msg, lite_actor_t* la) noexcept {
		la->msg_end = msg;
	}
};

//----------------------------------------------------------------------------------
//----- ПОТОКИ ---------------------------------------------------------------------
//----------------------------------------------------------------------------------

class alignas(64) lite_thread_t {
	size_t num;					// Номер потока
	std::atomic<bool> is_free;	// Поток свободен
	std::mutex mtx_sleep;		// Для засыпания
	std::condition_variable cv;	// Для засыпания
	std::atomic<bool> is_end;	// Поток зевершен

	// Конструктор
	lite_thread_t(size_t num) : num(num), is_free(true), is_end(false) { }

	// Общие данные всех потоков
	struct static_info_t {
		std::vector<lite_thread_t*> worker_list;	// Массив описателей потоков
		std::atomic<lite_thread_t*> worker_free = {0}; // Указатель на свободный поток
		//lite_mutex_t mtx;							// Блокировка доступа к массиву потоков
		std::shared_mutex mtx;						// Блокировка доступа к массиву потоков
		std::atomic<bool> stop = {0};				// Флаг остановки всех потоков
		std::atomic<size_t> thread_count = { 0 };	// Количество потоков
		std::atomic<size_t> thread_work = { 0 };	// Количество работающих потоков
		std::mutex mtx_end;							// Для ожидания завершения потоков
		std::condition_variable cv_end;				// Для ожидания завершения потоков
	};

	static static_info_t& si() {
		static static_info_t x;
		return x;
	}

	// Создание потока
	static void create_thread() noexcept {
		lite_thread_t* lt;
		{
			//lite_lock_t lck(si().mtx); // Блокировка
			std::unique_lock<std::shared_mutex> lck(si().mtx); // Блокировка
			size_t num = si().thread_count;
			si().thread_count++;
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
		}
		std::thread th(thread_func, lt);
		th.detach();
		#ifdef STAT_LT
		stat_thread_create++;
		size_t cnt = si().thread_count;
		if (stat_thread_max < cnt) stat_thread_max = cnt;
		#endif
	}

	// Поиск свободного потока
	static lite_thread_t* find_free() noexcept {
		lite_thread_t* wf = si().worker_free.exchange(NULL);
		if(wf != NULL && wf->is_free) return wf;

		if (si().thread_count == 0) return NULL;

		//lite_lock_t lck(si().mtx); // Блокировка
		std::shared_lock<std::shared_mutex> lck(si().mtx); // Блокировка
		size_t max = si().thread_count;
		assert(max <= si().worker_list.size());

		for (size_t i = 0; i < max; i++) {
			lite_thread_t* w = si().worker_list[i];
			if (w->is_free) {
				wf = w;
				break;
			}
		}

		si().worker_free = wf;
		return wf;
	}

	// Пробуждение свободного потока
	static void wake_up() noexcept {
		lite_thread_t* wf = find_free();
		if(wf != NULL) {
			wf->cv.notify_one();
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
	}

	// Функция потока
	static void thread_func(lite_thread_t* const lt) noexcept {
		#ifdef DEBUG_LT
		printf("%5lld: thread#%d start\n", lite_time_now(), lt->num);
		#endif
		this_num(lt->num);
		// Цикл обработки сообщений
		while(true) {
			// Проверка необходимости и создание новых потоков
			lite_actor_t* la = lite_actor_t::find_ready();
			if(la != NULL) { // Есть что обрабатывать
				lt->is_free = false;
				si().thread_work++;
				if (si().thread_work == si().thread_count && !si().stop) create_thread();
				// Обработка сообщений
				work_msg(la);
				#ifdef STAT_LT
				size_t t = si().thread_work;
				if (stat_parallel_run < t) stat_parallel_run = t;
				#endif		
				si().thread_work--;
				lt->is_free = true;
			}
			if (si().stop) break;
			// Уход в ожидание
			bool stop = false;
			{
				#ifdef _DEBUG
				printf("%5lld: thread#%d sleep\n", lite_time_now(), lt->num);
				#endif
				if(si().thread_work == 0) si().cv_end.notify_one(); // Если никто не работает, то разбудить ожидание завершения
				lite_thread_t* wf = si().worker_free;
				while(wf == NULL || wf->num > lt->num) { // Следующим будить поток с меньшим номером
					si().worker_free.compare_exchange_weak(wf, lt);
				}
				std::unique_lock<std::mutex> lck(lt->mtx_sleep);
				lt->is_free = true;
				if(lt->cv.wait_for(lck, std::chrono::seconds(1)) == std::cv_status::timeout) {	// Проснулся по таймауту
					#ifdef DEBUG_LT
					printf("%5lld: thread#%d wake up (total: %d, work: %d)\n", lite_time_now(), lt->num, (int)si().thread_count, (int)si().thread_work);
					#endif
					stop = (lt->num == si().thread_count - 1);	// Остановка потока с наибольшим номером
				} else {
					#ifdef _DEBUG
					printf("%5lld: thread#%d wake up\n", lite_time_now(), lt->num);
					#endif
					#ifdef STAT_LT
					stat_thread_wake_up++;
					#endif
				}
				if (si().worker_free == lt) si().worker_free = NULL;
				wf = lt;
				si().worker_free.compare_exchange_weak(wf, NULL);
			}
			if (stop) {
				lt->is_free = false;
				break;
			}
		}
		#ifdef _DEBUG
		printf("%5lld: thread#%d stop\n", lite_time_now(), lt->num);
		#endif

		//lite_lock_t lck(si().mtx); // Блокировка
		std::shared_lock<std::shared_mutex> lck(si().mtx); // Блокировка
		lt->is_end = true;
		si().thread_count--;
		si().cv_end.notify_one();
	}

public: //-------------------------------------
	// Помещение в очередь для последующего запуска
	static void run(lite_msg_t* msg, lite_actor_t* la) noexcept {
		if ((la->push(msg) && la->is_ready())) wake_up();
	}

	// Завершение, ожидание всех потоков
	static void end() noexcept {
		#ifdef DEBUG_LT
		printf("%5lld: --- wait all ---\n", lite_time_now());
		#endif	
		// Ожидание завершения расчетов. 
		while(si().thread_work > 0) {
			std::unique_lock<std::mutex> lck(si().mtx_end);
			si().cv_end.wait_for(lck, std::chrono::milliseconds(300));
		}
		#ifdef DEBUG_LT
		printf("%5lld: --- stop all ---\n", lite_time_now());
		#endif	
		// Остановка потоков
		si().stop = true;
		while(true) { // Ожидание остановки всех потоков
			bool is_end = true;
			{
				//lite_lock_t lck(si().mtx); // Блокировка
				std::shared_lock<std::shared_mutex> lck(si().mtx); // Блокировка
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
		assert(si().thread_count == 0);
		// Очистка памяти
		std::unique_lock<std::shared_mutex> lck(si().mtx); // Блокировка
		for (auto& w : si().worker_list) {
			delete w;
			w = NULL;
		}
		// Дообработка необработанных сообщений. В т.ч. о завершении работы
		work_msg();
		// Очистка памяти под акторы
		lite_actor_t::clear();
		#ifdef STAT_LT
		print_stat();
		#endif		
		#ifdef DEBUG_LT
		printf("%5lld: !!! end !!!\n", lite_time_now());
		if (lite_msg_t::used_msg() != 0) printf("ERROR: in memory %d messages\n", lite_msg_t::used_msg());
		assert(lite_msg_t::used_msg() == 0); // Остались не удаленные сообщения 
		#endif
		si().stop = false;
	}

	// Номер текущего потока
	static size_t this_num(size_t num = 99999999) noexcept {
		thread_local size_t n = 99999999;
		if (num != 99999999) {
			n = num;
		}
		return n;
	}
};

//----------------------------------------------------------------------------------
//----- ОБЕРТКИ --------------------------------------------------------------------
//----------------------------------------------------------------------------------
// Выделение памяти под сообщение, в случае ошибки возвращает NULL
static lite_msg_t* lite_msg_create(size_t size, int type = 0) noexcept {
	return lite_msg_t::create(size, type);
}

template <typename T>
static lite_msg_t* lite_msg_create(int type = 0) noexcept {
	return lite_msg_t::create<T>(type);
}

// Указатель на данные сообщения, NULL если размер не совпадает
template <typename T>
static T* lite_msg_data(lite_msg_t* msg) noexcept {
	return lite_msg_t::get<T>(msg);
}

// Удаление сообщения
static void lite_msg_erase(lite_msg_t* msg) noexcept {
	lite_msg_t::erase(msg);
}

// Копирование сообщения
static lite_msg_t* lite_msg_copy(lite_msg_t* msg) noexcept {
	return lite_actor_t::msg_copy(msg);
}

// Получения указателя на актор
static lite_actor_t* lite_actor_get(lite_func_t func, void* env = NULL) noexcept {
	return lite_actor_t::get(func, env);
}

// Установка глубины распараллеливания
static void lite_actor_parallel(int count, lite_actor_t* la) noexcept {
	lite_actor_t::parallel(count, la);
}

static void lite_actor_parallel(int count, lite_func_t func, void* env = NULL) noexcept {
	lite_actor_t::parallel(count, lite_actor_t::get(func, env));
}

// Отправка на выполнение
static void lite_thread_run(lite_msg_t* msg, lite_actor_t* la) noexcept {
	lite_thread_t::run(msg, la);
}

static void lite_thread_run(lite_msg_t* msg, lite_func_t func, void* env = NULL) noexcept {
	lite_thread_t::run(msg, lite_actor_t::get(func, env));
}

// Регистрация сообщения об окончании работы
static void lite_msg_end(lite_msg_t* msg, lite_actor_t* la) noexcept {
	lite_actor_t::actor_end(msg, la);
}
static void lite_msg_end(lite_msg_t* msg, lite_func_t func, void* env = NULL) noexcept {
	lite_actor_t::actor_end(msg, lite_actor_t::get(func, env));
}

// Завершение с ожиданием всех
static void lite_thread_end() noexcept {
	lite_thread_t::end();
}

// Номер текущего потока
static size_t lite_thread_num() noexcept {
	return lite_thread_t::this_num();
}

