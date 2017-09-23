/**
 Поиск уникальных строк в текстовом файле

 Первый шаг: 
 1. reader_t - последовательное чтение исходного файла блоками BLOCK_SIZE байт с выравниванием
    по концу последней строки внутри блока.
 2. parser_t (потоков по количеству ядер) - разбор блока на строки, расчет размера строк,
    хэша и построение индекса блока.
 3. bitmap_filter_t - выделение биткартой строк с уникальным хэшем и запись в результат, строки с повтором хэша на п.4
 4. double_filter_t - сохранение повторов в std::unordered_set<row_t> 

 Второй шаг:
 5. reader_t - последовательное чтение блоками найденных уникальных строк из результата п.3.
 6. parser_t (потоков по количеству ядер) - разбор блока на строки, расчет размера строк,
    хэша и построение индекса блока.
 7. double_filter_t - удаление из std::unordered_set<row_t> строк присутствующих в результате.
 8. double_filter_t - сохранение std::unordered_set<row_t> в результат.

 Для избежания больших очередей сообщений из-за разной скорости работы: в начале каждого шага
 запускаются BLOCK_COUNT сообщений-буферов блоков (msg_t) и передаются по кругу.

 Блок с size == 0 - сообщение следующему об окончании шага.
*/

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>   
#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <vector>
#include <unordered_set>
#include "hash.h"
#include "bitmap.h"
#include "block_write.h"
#include "str_cache.h"
#include "mem_used.h"

//#define LT_STAT
//#define LT_DEBUG_LOG
#ifdef NDEBUG
#undef NDEBUG
#endif

#include "../lite_thread_util.h"

#define BLOCK_SIZE 0x40000	// Размер блока читаемого за один раз
#define BLOCK_COUNT 100		// Количество блоков, используемых во время работы

//----------------------------------------------------------------------
// Информация о строке внутри блока
struct row_info_t {
	int str_pos;
	int len;
	hash_t hash;

	row_info_t() {	}

	row_info_t(size_t str_pos, size_t len, hash_t hash) {
		this->hash = hash;
		this->len = (int)len;
		this->str_pos = (int)str_pos;
	}

	row_info_t& operator=(const row_info_t& r) {
		hash = r.hash;
		len = r.len;
		str_pos = r.str_pos;
		return *this;
	}
};

//----------------------------------------------------------------------
// Строка для помещения в std::unordered_set
struct row_t {
	const char* str;
	int len;
	hash_t hash;

	row_t(const char* str, int len, hash_t hash) {
		this->str = str;
		this->len = len;
		this->hash = hash;
	}

	bool operator==(const row_t& r) const {
		if(hash != r.hash || len != r.len) {
			return false;
		} else {
			return memcmp(str, r.str, len) == 0;
		}
	}
};

namespace std {
	template<> class hash<row_t> {
	public:
		size_t operator()(const row_t &x) const {
			return x.hash;
		}
	};
};


//-----------------------------------------------------------------------
// Прочитанный из файла блок
struct msg_t : public lite_msg_t {
	char data[BLOCK_SIZE];	// Данные
	size_t size;			// Размер прочитанного в data
	std::vector<row_info_t> idx; // Индекс строк в data
	size_t total;			// Для передачи количества прочитанных блоков (при size == 0)
};

// Состояние расчета
struct msg_stat_t : public lite_msg_t {
	size_t read_bytes = { 0 };	// Прочитано байт
	size_t read_rows = { 0 };	// Прочитано строк
	size_t uniq_rows = { 0 };	// Найдено уникальных строк
	size_t uniq_rows2 = { 0 };	// Найдено уникальных строк (Шаг 2)
	hash_t checksum = { 0 };	// Контрольная сумма найденных уникальных
};


//------------------------------------------------------------------------------------
// Чтение файла блоками с выравниванием по концу строки
class reader_t : public lite_actor_t {
	FILE* f = {NULL};
	lite_actor_t* next = { NULL };	// Следующий обработчик
	size_t total = {0};				// Обработано блоков
	msg_t* m_next = {NULL};			// Кэш следующего блока

	size_t read_bytes = { 0 };
	lite_actor_t* stat = { NULL };

public:
	reader_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	// Инициализация
	void init(FILE* file, lite_actor_t* next, lite_actor_t* stat) {
		f = file;
		this->next = next;
		this->stat = stat;
		total = 0;
	}

	// Чтение блока из файла и выравнивание по концу последней полной строки
	void read_block(msg_t* m) noexcept {
		if (f == NULL) return;

		m->size = 0;

		if (m_next == NULL) {
			m_next = lite_msg_copy(m);
			return;
		}

		size_t read = fread(m_next->data + m_next->size, 1, BLOCK_SIZE - m_next->size, f);
		if (read + m_next->size != BLOCK_SIZE && read != 0 && m_next->data[read + m_next->size - 1] != 0xA && m_next->data[read + m_next->size - 1] != 0xD) { // Последний блок
			m_next->data[read + m_next->size] = 0xD;
			read++;
		}
		m_next->size += read;
		if (read == 0) {
			if (m_next->size == 0) {
				lite_log(0, "Read end. Total %llu Bytes. Time: %llu ms. Mem %llu Mb    ", read_bytes, lite_time_now(), mem_used() / 1024);
				finish();
				return;
			}
		} else {
			char* p = m_next->data + m_next->size - 1;
			while (p >= m_next->data && *p != 0xA && *p != 0xD) p--;
			if (p < m_next->data) {
				lite_log(LITE_ERROR_USER, "Row is too large. Max %d", BLOCK_SIZE);
				finish();
				return;
			} else {
				p++;
				m->size = m_next->size - (p - m_next->data);
				if (m->size > 0) {
					memcpy(m->data, p, m->size);
					m_next->size -= m->size;
				}
			}
		}

		next->run(m_next);
		m_next = lite_msg_copy(m);
		total++;

		// Статистика прочитанного
		read_bytes += read;
		if((total & 0xFF) == 0) {
			msg_stat_t* ms = new msg_stat_t();
			ms->read_bytes = read_bytes;
			stat->run(ms);
		}
	}

	// Завершение чтения
	void finish() noexcept {
		if (m_next == NULL) return;

		m_next->size = 0;
		m_next->total = total;
		next->run(m_next);
		m_next = NULL;
		f = NULL;
	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		read_block(m);
	}

};

//------------------------------------------------------------------------------------
// Разбивка буфера на строки и расчет хэшей
class parser_t : public lite_actor_t {
	lite_actor_t* next; // Следующий обработчик
public:
	parser_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	// Инициализация
	void init(lite_actor_t* next) {
		this->next = next;
	}

	// Разбор блока на строки и расчет хэша
	void parse(msg_t* m) noexcept {
		m->idx.clear();

		char* cur = m->data;
		char* end = m->data + m->size;
		while (cur < end && (*cur == 0xA || *cur == 0xD)) cur++;
		while (cur < end) {
			char* p = cur;
			while (p < end && *p != 0xA && *p != 0xD) p++;
			assert(p != end);
			hash_t h = hash32(cur, p - cur);
			size_t len = p - cur;
			m->idx.push_back(row_info_t(cur - m->data, len, h));
			cur = p + 1;
			while (cur < end && (*cur == 0xA || *cur == 0xD)) cur++;
		}
		next->run(m);

	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg); // Указатель на содержимое
		if(m->size != 0) {
			parse(m);
		} else {
			next->run(m);
		}
	}
};

//------------------------------------------------------------------------------
// Выделение уникальных биткартой
class bitmap_filter_t : public lite_actor_t {
	bitmap_t bm_uniq;		// Биткарта для фильтрации уникальных по хэшу
	FILE* f_out = { NULL };	// Файл, для записи найденных строк
	block_write_t bw;		// Блочная запись в файл

	size_t uniq_count = { 0 };	// Найдено уникальных
	size_t dbl_count = { 0 };	// Найдено повторов и коллизий

	size_t total = { 0 };		// Обработано блоков
	size_t total_check = { 0 };	// Общее количество блоков
	size_t total_dbls = { 0 };	// Количество блоков с повторами

	hash_t checksum = { 0 };	// Контрольная сумма найденных уникальных

	lite_actor_t* dbl_filter = { NULL };	// Обработчик повторов
	lite_actor_t* reader = { NULL };		// Для возврата обработанных блоков
	lite_actor_t* stat = { NULL };			// Обработчик статистики

	msg_t* msg_last = { NULL };				// Кэш для последнего сообщения

public:
	bitmap_filter_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	void init(FILE* f_out, lite_actor_t* reader, lite_actor_t* dbl_filter, lite_actor_t* stat) {
		this->f_out = f_out;
		bw.init(BLOCK_SIZE, f_out);
		this->reader = reader;
		this->dbl_filter = dbl_filter;
		this->stat = stat;
		bm_uniq.init(0xFFFFFFFF);
	}

	// Проверка хэшей по биткарте
	void parse(msg_t* m) noexcept {
		size_t dbl_idx = 0;
		for(auto& r : m->idx) {
			char* str = m->data + r.str_pos;
			if (!bm_uniq.getset(r.hash)) { // хэш встретился впервые
				bw.write_str(str, r.len);
				uniq_count++;
				checksum ^= r.hash;
			} else { // Повтор хэша
				m->idx[dbl_idx] = r;
				dbl_idx++;
			}
		}

		if(dbl_idx != 0) { // Были повторы. Отправка на обработку
			m->idx.resize(dbl_idx);
			dbl_count += dbl_idx;
			total_dbls++;
			dbl_filter->run(m);
		} else {
			reader->run(m);
		}

		// Статистика 
		if ((total & 0xFF) == 0) {
			msg_stat_t* ms = new msg_stat_t();
			ms->uniq_rows = uniq_count;
			ms->read_rows = uniq_count + dbl_count;
			stat->run(ms);
		}

	}

	// Завершение обработки
	void finish() {
		bw.flush();
		lite_log(0, "Bitmap end. Time: %llu ms. Uniq %llu rows. Dbls %llu rows. Total %llu rows.", lite_time_now(), uniq_count, dbl_count, uniq_count + dbl_count);
		msg_last->total = total_dbls;
		bm_uniq.clear();
		fseek(f_out, SEEK_SET, 0);
		f_out = NULL;
		dbl_filter->run(msg_last);
		msg_last = NULL;

		msg_stat_t* ms = new msg_stat_t();
		ms->uniq_rows = uniq_count;
		ms->checksum = checksum;
		stat->run(ms);
	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		if (m->size == 0) {
			total_check = m->total;
			msg_last = lite_msg_copy(m);
		} else {
			parse(m);
			total++;
		}

		if (total_check == total) { // Обработаны все блоки
			finish();
		}
	}
};

//------------------------------------------------------------------------------
// Обработка повторов хэша
typedef std::unordered_set<row_t> dbl_list_t;

#define BLOOM_MASK 0x1FFFFFFF // Для биткарты 32 Мб

class double_filter_t : public lite_actor_t {
	block_write_t bw;		// Для вывода результата
	int step = {0};			// Номер прохода

	dbl_list_t dbl_list;	// Список повторов
	str_cache_t str_cache;	// Кэш строк

	lite_actor_t* reader = { NULL }; // Для возврата обработанных блоков
	lite_actor_t* reader2 = { NULL }; // Для возврата обработанных блоков (Шаг 2)
	lite_actor_t* stat = { NULL };	// Вывод статистики

	size_t dbl_count = { 0 }; // Обработано строк
	size_t total = { 0 };		// Обработано блоков
	size_t total_check = { 0 };	// Общее количество блоков

	bitmap_t bm_bloom;	// Биткарта под фильтр блума

	// Запись хэша в биткарту
	void bloom_set(hash_t hash) {
		bm_bloom.set(hash & BLOOM_MASK);
		bm_bloom.set((hash >> 2) & BLOOM_MASK);
		bm_bloom.set((hash >> 4) & BLOOM_MASK);
	}

	// Проверка хэша в биткарте (возможно ложноположительное срабатывание)
	bool bloom_get(hash_t hash) {
		return bm_bloom.get(hash & BLOOM_MASK) && bm_bloom.get((hash >> 2) & BLOOM_MASK) && bm_bloom.get((hash >> 4) & BLOOM_MASK);
	}

public:
	double_filter_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	void init(FILE* f_out, lite_actor_t* reader, lite_actor_t* reader2, lite_actor_t* stat) {
		bw.init(BLOCK_SIZE, f_out);
		bm_bloom.init(BLOOM_MASK);
		this->reader = reader;
		this->reader2 = reader2;
		this->stat = stat;
	}

	// Переключение на Шаг 2, проход по уже найденным уникальным
	void next_step() {
		if (step != 0) return;
		step = 1;
		total = 0;
		total_check = 0;
		dbl_count = 0;
		for (size_t i = 0; i < BLOCK_COUNT; i++) reader2->run(new msg_t());
	}

	// Сохранение уникальных среди повторов (Шаг 1)
	void parse1(msg_t* m) noexcept {
		for (auto& r : m->idx) {
			bloom_set(r.hash);
			char* str = m->data + r.str_pos;
			row_t row(str, r.len, r.hash);
			if(dbl_list.find(row) == dbl_list.end()) {
				row.str = str_cache.add(str, r.len);
				dbl_list.insert(row);
			}
		}
		dbl_count += m->idx.size();

		reader->run(m);
	}

	// Проверка уникальных среди повторов (Шаг 2)
	void parse2(msg_t* m) noexcept {
		for (auto& r : m->idx) {
			if(bloom_get(r.hash)) {
				dbl_list_t::iterator it = dbl_list.find(row_t(m->data + r.str_pos, r.len, r.hash));
				if (it != dbl_list.end()) {
					dbl_list.erase(it);
				}
			}
		}
		
		dbl_count += m->idx.size();

		reader2->run(m);
	}

	// Сохранение результата
	void finish() {
		hash_t checksum = 0;
		for (auto& r : dbl_list) {
			bw.write_str(r.str, r.len);
			checksum ^= r.hash;
		}
		bw.flush();

		msg_stat_t* ms = new msg_stat_t();
		ms->uniq_rows2 = dbl_list.size();
		ms->checksum = checksum;
		stat->run(ms);
	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		if (m->size == 0) {
			total_check = m->total;
		} else {
			total++;
			if(step == 0) {
				parse1(m);
			} else {
				parse2(m);
				// Статистика 
				if ((total & 0xFF) == 0) {
					msg_stat_t* ms = new msg_stat_t();
					ms->read_rows = dbl_count;
					stat->run(ms);
				}
			}
		}

		if(total == total_check) {
			if (step == 0) {
				lite_log(0, "Doubles end. Time: %llu ms. Dbls %llu rows", lite_time_now(), dbl_list.size());
				next_step();
			} else {
				lite_log(0, "Doubles end. Time: %llu ms. Uniq %llu rows", lite_time_now(), dbl_list.size());
				finish();
			}
		}
	}
};


//--------------------------------------------------------------------------------------------------
// Подсчет количества строк в файле и XOR(hash32()) всех строк
class checker_t : public lite_actor_t {
	hash_t checksum = { 0 };
	size_t count = { 0 };
	size_t block_cnt = { 0 };

	size_t total = { 0 };
	size_t total_check = { 0 };
	lite_actor_t* reader = {NULL};
	lite_actor_t* stat = { NULL };


public:
	checker_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
	}

	// Инициализация
	void init(lite_actor_t* reader, lite_actor_t* stat) {
		this->reader = reader;
		this->stat = stat;
	}

	// Обработка блока
	void calc_block(msg_t* m) noexcept {
		for (auto& r : m->idx) {
			checksum ^= r.hash;
		}
		count += m->idx.size();
	}


	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		if(m->size == 0) {
			total_check = m->total;
		} else {
			calc_block(m);
			total++;
		}
		if(total_check == total) {
			msg_stat_t* ms = new msg_stat_t();
			ms->uniq_rows = count;
			ms->checksum = checksum;
			stat->run(ms);
		}
		reader->run(m);
	}

};

//--------------------------------------------------------------------------------------------------
// Хранение и вывод текущего состояния расчета
class stat_t : public lite_actor_t {
	msg_stat_t stat;
	int64_t time = {0};

public:
	stat_t() {
		type_add(lite_msg_type<msg_stat_t>()); // Разрешение принимать сообщение типа msg_t
	}

	~stat_t() {
		lite_log(0, "Time: %llu ms. Unique %llu rows. Checksum %08X", lite_time_now(), stat.uniq_rows + stat.uniq_rows2, stat.checksum);
	}

	void timer() override {
		printf("Time %llu ms. Read: %llu Mb %llu Krows. Uniq %llu Krows. Mem %llu Mb.\r", lite_time_now(), stat.read_bytes / 0x100000, stat.read_rows / 1000, stat.uniq_rows / 1000, mem_used() / 1024);
	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_stat_t* m = static_cast<msg_stat_t*>(msg);
		if (m->read_bytes != 0) stat.read_bytes = m->read_bytes;
		if (m->uniq_rows != 0) stat.uniq_rows = m->uniq_rows;
		if (m->uniq_rows2 != 0) stat.uniq_rows2 = m->uniq_rows2;
		if (m->read_rows != 0) stat.read_rows = m->read_rows;
		if (m->checksum != 0) stat.checksum ^= m->checksum;
	}
};

//--------------------------------------------------------------------------------------------------
// Чтение файла
void read_file(const char* filename, const char* filename_out, int threads) {
	lite_time_now();
	FILE* f = fopen(filename, "rb");
	if (f == NULL) {
		lite_log(0, "Not open %s", filename);
		return;
	}
	FILE* f_out = fopen(filename_out, "wb+");
	if (f_out == NULL) {
		printf("Not open %s\n", filename_out);
		fclose(f);
		return;
	}
	lite_log(0, "Read file %s use %d threads", filename, threads);

	lite_thread_max(threads); // Ограничение кол-ва потоков
	// Акторы
	reader_t* reader = new reader_t();
	reader->name_set("reader");
	reader->resource_set(lite_resource_create("HDD", 1));

	parser_t* parser = new parser_t();
	parser->name_set("parser");
	parser->parallel_set(threads);

	bitmap_filter_t* bm_filter = new bitmap_filter_t();
	bm_filter->name_set("bitmap filter");

	double_filter_t* dbl_filter = new double_filter_t();
	dbl_filter->name_set("double filter");

	stat_t* stat = new stat_t();
	stat->name_set("stat");
	stat->timer_set(500);
	stat->resource_set(lite_resource_create("STAT", 1));

	reader_t* reader2 = new reader_t();
	reader2->name_set("reader 2");
	reader2->resource_set(lite_resource_create("HDD", 1));

	parser_t* parser2 = new parser_t();
	parser2->name_set("parser 2");
	parser2->parallel_set(threads);

	// Инициализация
	reader->init(f, parser, stat);
	parser->init(bm_filter);
	bm_filter->init(f_out, reader, dbl_filter, stat);
	dbl_filter->init(f_out, reader, reader2, stat);
	reader2->init(f_out, parser2, stat);
	parser2->init(dbl_filter);

	// Запуск расчета
	for (size_t i = 0; i < BLOCK_COUNT; i++) reader->run(new msg_t());

	lite_thread_end(); // Ожидание окончания расчета

	fclose(f);
	fclose(f_out);
}

//--------------------------------------------------------------------------------------------------
// Контрольная сумма файла
void file_checksum(const char* filename, int threads) {
	lite_time_now();
	FILE* f = fopen(filename, "rb");
	if (f == NULL) {
		lite_log(0, "Not open %s", filename);
		return;
	}
	lite_log(0, "Check file %s use %d threads", filename, threads);

	lite_thread_max(threads); // Ограничение кол-ва потоков
	
	// Акторы
	reader_t* reader = new reader_t();
	reader->name_set("reader");
	reader->resource_set(lite_resource_create("HDD", 1));

	parser_t* parser = new parser_t();
	parser->name_set("parser");
	parser->parallel_set(threads);

	checker_t* checker = new checker_t();
	checker->name_set("checker");

	stat_t* stat = new stat_t();
	stat->name_set("stat");
	stat->timer_set(500);
	stat->resource_set(lite_resource_create("STAT", 1));

	reader->init(f, parser, stat);
	parser->init(checker);
	checker->init(reader, stat);

	// Запуск расчета
	for (size_t i = 0; i < BLOCK_COUNT; i++) reader->run(new msg_t());

	lite_thread_end(); // Ожидание окончания расчета

	fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		printf("\n\n Select unique rows from text file. \n\n Command line:\n uniq.exe source_file result_file\n");
		printf("\n\n Calc checksum and rows count from text file. \n\n Command line:\n uniq.exe source_file\n");
		return 0;
	}
	printf("Compile %s %s\n", __DATE__, __TIME__);
	if (argc == 2) {
		file_checksum(argv[1], lite_processor_count());
		return 0;
	}

	read_file(argv[1], argv[2], lite_processor_count());

#ifdef _DEBUG
	system("pause");
#endif
	return 0;
}