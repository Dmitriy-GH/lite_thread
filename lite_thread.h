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

#ifdef _DEBUG
#ifndef DEBUG_LT
#define DEBUG_LT
#endif
#endif

#ifdef STAT_LT
// Счетчики для отладки
std::atomic<uint32_t> stat_thread_create;	// Создано потоков
std::atomic<uint32_t> stat_thread_wake_up;	// Сколько раз будились потоки
std::atomic<uint32_t> stat_parallel_run;	// Максимальное количество потоков работавших параллельно
std::atomic<uint32_t> stat_thread_max;		// Максимальное количество потоков запущенных одновременно
std::atomic<uint32_t> stat_msg_create;		// Создано сообщений
std::atomic<uint32_t> stat_queue_push; 
std::atomic<uint32_t> stat_actor_get;
std::atomic<uint32_t> stat_msg_not_run;
std::atomic<uint32_t> stat_msg_run;
std::atomic<uint32_t> stat_actor_find;

void print_stat() {
	printf("\n------- STAT -------\n");
	printf("thread_create  %u\n", (uint32_t)stat_thread_create);
	printf("thread_wake_up %u\n", (uint32_t)stat_thread_wake_up);
	printf("thread_max     %u\n", (uint32_t)stat_thread_max);
	printf("parallel_run   %u\n", (uint32_t)stat_parallel_run);
	printf("msg_create     %u\n", (uint32_t)stat_msg_create);
	printf("queue_push     %u\n", (uint32_t)stat_queue_push);
	printf("actor_get      %u\n", (uint32_t)stat_actor_get);
	printf("msg_not_run    %u\n", (uint32_t)stat_msg_not_run);
	printf("msg_run        %u\n", (uint32_t)stat_msg_run);
	printf("actor_find     %u\n", (uint32_t)stat_actor_find);
}
#endif

//----------------------------------------------------------------------------------
//------ ОБЩЕЕ ---------------------------------------------------------------------
//----------------------------------------------------------------------------------
class lock_t;

// Блокировка без переключения в режим ядра
class spin_lock_t { // Взято тут http://anki3d.org/spinlock/
	std::atomic_flag lck = ATOMIC_FLAG_INIT;
	friend lock_t;
protected:
	void lock() noexcept {
		while (lck.test_and_set(std::memory_order_acquire)) {}
	}

	void unlock() noexcept {
		lck.clear(std::memory_order_release);
	}
};

class lock_t {
	spin_lock_t* mtx;
public:
	lock_t(spin_lock_t& mtx) noexcept {
		this->mtx = &mtx;
		this->mtx->lock();
	}

	~lock_t() noexcept {
		this->mtx->unlock();
	}
};

// Время с момента запуска, мсек
static int time_now() {
	static std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point t2 = std::chrono::steady_clock::now();
	std::chrono::duration<double> time_span = std::chrono::duration_cast<std::chrono::duration<double>>(t2 - t);
	return (int)(time_span.count() * 1000);
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
			//msg->actor = NULL;
			if (size != 0) msg->data[0] = 0;
		}
		#ifdef DEBUG_LT
		used_msg(1);
		//printf("%5d: create msg#%p type#%X\n", time_now(), msg, msg->type);
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
			//printf("%5d: delete msg#%p\n", time_now(), msg);
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
	friend lite_thread_t;
protected:
	std::queue<lite_msg_t*> msg_queue;	// Очередь сообщений
	std::atomic<lite_msg_t*> msg_one;	// Альтернатива очереди при msg_count == 1
	spin_lock_t mtx;					// Синхронизация доступа к очереди
	//std::mutex mtx;					// Синхронизация доступа к очереди
	lite_actor_func_t la_func;			// Функция с окружением
	std::atomic<int> msg_count;			// Сообщений в очереди
	std::atomic<int> actor_free;		// Количество свободных акторов, т.е. сколько можно запускать
	std::atomic<int> thread_max;		// Количество потоков, в скольки можно одновременно выполнять
	std::atomic<lite_msg_t*> msg_end;	// Сообщение об окончании работы

	//---------------------------------
	// Конструктор
	lite_actor_t(const lite_actor_func_t& la) : la_func(la), actor_free(1), thread_max(1), msg_count(0), msg_end(0) {	}

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
		return (msg_count > 0 && actor_free > 0);
	}

	// Постановка сообщения в очередь, возврашает true если надо будить другой поток
	bool push(lite_msg_t* msg) noexcept {
		{
			//std::unique_lock<std::mutex> lck(mtx);
			lock_t lck(mtx); // Блокировка
			switch(msg_count) {
			case 0:
				msg_one = msg;
				break;

			case 1:
				#ifdef DEBUG_LT
				assert(msg_one != (lite_msg_t*)NULL);
				#endif
				msg_queue.push(msg_one);
				msg_queue.push(msg);
				break;

			default:
				msg_queue.push(msg);
			}
			msg_count++;
		}

		if (msg == ti().msg_del) {
			// Помеченное на удаление сообщение поместили в очередь другого актора. Снятие пометки
			ti().msg_del = NULL;
		} else if (ti().msg_del == NULL) {
			// Не было предыдушего сообщения, отправка из чужого потока
			ti().need_wake_up = true;
		}

		bool need_wake_up = ti().need_wake_up;
		if(actor_free > 0) {
			// Актор готов запуститься
			ti().need_wake_up = true;
			if (ti().la_next_run == NULL) ti().la_next_run = this;
		}
		#ifdef STAT_LT
		if(msg_count > 1) stat_queue_push++;
		#endif
		return need_wake_up;
	}

	// Получение сообщения из очереди
	lite_msg_t* pop() noexcept {
		lite_msg_t* msg = NULL;
		lock_t lck(mtx); // Блокировка
		//std::unique_lock<std::mutex> lck(mtx);
		if (msg_count == 0) {
			assert(msg_queue.size() == 0);
		} else {
			switch(msg_count) {
			case 1:
				msg = msg_one;
				#ifdef DEBUG_LT
				msg_one = NULL;
				#endif
				break;

			case 2:
				msg = msg_queue.front();
				msg_queue.pop();
				msg_one = msg_queue.front();
				msg_queue.pop();
				break;

			default:
				msg = msg_queue.front();
				msg_queue.pop();
			}

			msg_count--;
		}
		return msg;
	}

	// Запуск обработки сообщения, если не задано то из очереди
	void run() noexcept {
		lite_msg_t* msg = NULL;
		actor_free--;
		if (actor_free < 0) {
			// Уже выполняется разрешенное количество акторов
			actor_free++;
			#ifdef STAT_LT
			stat_msg_not_run++;
			#endif
			return;
		}

		// Извлечение сообщения из очереди
		if (msg == NULL) msg = pop();

		if (msg != NULL) { // Запуск функции
			#ifdef STAT_LT
			stat_msg_run++;
			#endif
			ti().msg_del = msg; // Пометка на удаление
			ti().need_wake_up = false;
			la_func.func(msg, la_func.env); // Запуск
			if (msg == ti().msg_del) lite_msg_t::erase(msg);
		}
		actor_free++;
		return;
	}

	// static методы уровня потока -------------------------------------------------
	struct thread_info_t {
		lite_msg_t* msg_del;		// Обрабатываемое сообщение, будет удалено после обработки
		bool need_wake_up;			// Необходимо будить другой поток при отправке
		lite_actor_t* la_next_run;	// Следующий на выполнение актор
	};

	// Текущее сообщение в потоке
	static thread_info_t& ti() noexcept {
		thread_local thread_info_t ti = {0};
		return ti;
	}


	// static методы глобальные ----------------------------------------------------
	typedef std::unordered_map<lite_actor_func_t, lite_actor_t*> lite_actor_info_list_t;

	struct static_info_t {
		lite_actor_info_list_t la_list; // Индекс для поиска lite_actor_t*
		spin_lock_t mtx;				// Блокировка для доступа к la_list
		lite_actor_info_list_t::iterator la_iterator;
	};

	static static_info_t& si() noexcept {
		static static_info_t x;
		return x;
	}

	// Очистка всего
	static void clear() noexcept {
		lock_t lck(si().mtx); // Блокировка

		for(auto& a : si().la_list) {
			delete a.second;
		}
		si().la_list.clear();
		si().la_iterator = si().la_list.begin();
	}

	// Поиск ожидающего выполнение
	static lite_actor_t* find_ready() noexcept {
		lite_actor_t* ret = ti().la_next_run;
		if(ret != NULL && ret->msg_count > 0 && ret->is_ready()) {
			ti().la_next_run = NULL;
			return ret;
		}
		#ifdef STAT_LT
		stat_actor_find++;
		#endif
		// Поиск очередного свободного актора
		ret = NULL;

		lock_t lck(si().mtx); // Блокировка
		for(lite_actor_info_list_t::iterator it = si().la_iterator; it != si().la_list.end(); it++) {
			if (it->second->msg_count > 0 && it->second->is_ready()) {
				ret = it->second;
				it++;
				si().la_iterator = it;
				break;
			}
		}
		if(ret == NULL) {
			for (lite_actor_info_list_t::iterator it = si().la_list.begin(); it != si().la_iterator; it++) {
				if (it->second->msg_count > 0 && it->second->is_ready()) {
					ret = it->second;
					it++;
					si().la_iterator = it;
					break;
				}
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

		lock_t lck(si().mtx); // Блокировка
		lite_actor_info_list_t::iterator it = si().la_list.find(a);
		lite_actor_t* ai;
		if (it != si().la_list.end()) {
			ai = it->second;
		} else {
			ai = new lite_actor_t(a);
			si().la_list[a] = ai;
			si().la_iterator = si().la_list.begin();
		}
		return ai;
	}

	// Установка глубины распараллеливания
	static void parallel(int count, lite_actor_t* la) noexcept {
		if (count <= 0) count = 1;
		if (count == la->thread_max) return;

		//std::unique_lock<std::mutex> lck(la->mtx);
		lock_t lck(la->mtx); // Блокировка
		la->actor_free += count - la->thread_max;
		la->thread_max = count;
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
		spin_lock_t mtx;							// Блокировка доступа к массиву потоков
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
			lock_t lck(si().mtx); // Блокировка
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
		//printf("%5d: create() %d\n", time_now(), num);
		std::thread th(thread, lt);
		th.detach();
		#ifdef STAT_LT
		stat_thread_create++;
		size_t cnt = si().thread_count;
		if (stat_thread_max < cnt) stat_thread_max = cnt;
		#endif
	}

	// Поиск свободного потока
	static lite_thread_t* find_free() noexcept {
		lite_thread_t* wf = si().worker_free;
		if(wf != NULL && wf->is_free) return wf;

		if (si().thread_count == 0) return NULL;

		lock_t lck(si().mtx); // Блокировка
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
			la->run();
			la = lite_actor_t::find_ready();
		}
	}

	// Функция потока
	static void thread(lite_thread_t* lt) noexcept {
		#ifdef DEBUG_LT
		printf("%5d: thread#%d start\n", time_now(), lt->num);
		#endif
		this_num(lt->num);
		lt->is_free = false;
		// Цикл обработки сообщений
		while(true) {
			// Проверка необходимости и создание новых потоков
			lite_actor_t* la = lite_actor_t::find_ready();
			if(la != NULL) { // Есть что обрабатывать
				si().thread_work++;
				if (si().thread_work == si().thread_count && !si().stop) create_thread();
				// Обработка сообщений
				work_msg(la);
				#ifdef STAT_LT
				size_t t = si().thread_work;
				if (stat_parallel_run < t) stat_parallel_run = t;
				#endif		
				si().thread_work--;
			}
			if (si().stop) break;
			// Уход в ожидание
			{
				#ifdef DEBUG_LT
				printf("%5d: thread#%d sleep\n", time_now(), lt->num);
				#endif
				if(lt->num == 0) si().cv_end.notify_one(); // Нулевой поток будит ожидание завершения
				lite_thread_t* wf = si().worker_free;
				if (wf == NULL || wf->num > lt->num) si().worker_free = lt; // Следующим будить поток с меньшим номером
				std::unique_lock<std::mutex> lck(lt->mtx_sleep);
				lt->is_free = true;
				if(lt->cv.wait_for(lck, std::chrono::seconds(1)) == std::cv_status::timeout	// Проснулся по таймауту
							&& lt->num != 0													// Не нулевой поток
							&& lt->num == si().thread_count - 1								// Поток с большим номером
							&& si().thread_work < si().thread_count - 1) {					// Есть еще спящие потоки
					lt->is_free = false;
					if(find_free() != NULL) break; // Завершение потока
				}
				lt->is_free = false;
				if (si().worker_free == lt) si().worker_free = NULL;
				#ifdef DEBUG_LT
				printf("%5d: thread#%d wake up\n", time_now(), lt->num);
				#endif
				#ifdef STAT_LT
				stat_thread_wake_up++;
				#endif
			}
		}
		#ifdef DEBUG_LT
		printf("%5d: thread#%d stop\n", time_now(), lt->num);
		#endif

		lock_t lck(si().mtx); // Блокировка
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
		printf("%5d: --- wait all ---\n", time_now());
		#endif	
		// Ожидание завершения расчетов. Нулевой поток разбудит перед засыпанием
		while(si().thread_work > 0) {
			std::unique_lock<std::mutex> lck(si().mtx_end);
			si().cv_end.wait_for(lck, std::chrono::milliseconds(1000));
		}
		#ifdef DEBUG_LT
		printf("%5d: --- stop all ---\n", time_now());
		#endif	
		// Остановка потоков
		si().stop = true;
		while(true) { // Ожидание остановки всех потоков
			bool is_end = true;
			{
				lock_t lck(si().mtx); // Блокировка
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
		#if defined(_DEBUG) | defined(DEBUG_LT)
		printf("%5d: !!! end !!!\n", time_now());
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

