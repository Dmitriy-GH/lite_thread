/**
Исходный тест https://github.com/Mark-Kovalyov/CardRaytracerBenchmark/blob/master/cpp/card-raytracer.cpp

Тест производительности.

Запускать с параметром количество потоков

card_raytracer.exe [threads]

если указать 0 запустится оригинальный вариант без акторов
по умолчанию threads = 4 


*/

#define _CRT_SECURE_NO_WARNINGS
#include <stdlib.h>   
#include <stdio.h>
#include <math.h>
#include <assert.h>
#define LT_STAT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../lite_thread.h"

#define WIDTH  512
#define HEIGHT 512

#define FILE_NAME "1.ppm"


struct Vector {

	Vector() {
	}

	Vector(double a, double b, double c) {
		x = a;
		y = b;
		z = c;
	}

	void init(double a, double b, double c) {
		x = a;
		y = b;
		z = c;
	}

	double x, y, z;

	Vector operator+(const Vector &r) {
		return Vector(x + r.x, y + r.y, z + r.z);
	}

	Vector operator*(double r) {
		return Vector(x * r, y * r, z * r);
	}

	double operator%(const Vector &r) {
		return x * r.x + y * r.y + z * r.z;
	}

	Vector operator^(const Vector &r) {
		return Vector(y * r.z - z * r.y, z * r.x - x * r.z, x * r.y - y * r.x);
	}

	Vector operator!() {
		return *this * (1 / sqrt(*this % *this));
	}

	void print(FILE* out) {
		fprintf(out, "%c%c%c", (int)x, (int)y, (int)z);
	}
};

int G[] = {
	0x0003C712,  // 00111100011100010010 
	0x00044814,  // 01000100100000010100
	0x00044818,  // 01000100100000011000
	0x0003CF94,  // 00111100111110010100
	0x00004892,  // 00000100100010010010
	0x00004891,  // 00000100100010010001
	0x00038710,  // 00111000011100010000
	0x00000010,  // 00000000000000010000
	0x00000010,  // 00000000000000010000
};

double Random() {
	return (double)rand() / RAND_MAX;
}

int tracer(Vector o, Vector d, double &t, Vector& n) {
	t = 1e9;
	int m = 0;
	double p = -o.z / d.z;
	if (.01 < p) {
		t = p;
		n = Vector(0, 0, 1);
		m = 1;
	}
	for (int k = 19; k--;)
		for (int j = 9; j--;)
			if (G[j] & 1 << k) {
				Vector p = o + Vector(-k, 0, -j - 4);
				double b = p % d;
				double c = p % p - 1;
				double q = b * b - c;
				if (q > 0) {
					double s = -b - sqrt(q);
					if (s < t && s > .01) {
						t = s;
						n = !(p + d * t);
						m = 2;
					}
				}
			}
	return m;
}

Vector sampler(Vector o, Vector d) {
	double t;
	Vector n;
	int m = tracer(o, d, t, n);
	if (!m) {
		return Vector(.7, .6, 1) * pow(1 - d.z, 4);
	}
	Vector h = o + d * t;
	Vector l = !(Vector(9 + Random(), 9 + Random(), 16) + h * -1);
	Vector r = d + n * (n % d * -2);
	double b = l % n;
	if (b < 0 || tracer(h, l, t, n)) {
		b = 0;
	}
	double p = pow(l % r * (b > 0), 99);
	if (m & 1) {
		h = h * .2;
		return ((int)(ceil(h.x) + ceil(h.y)) & 1 ? Vector(3, 1, 1) : Vector(3, 3, 3)) * (b * .2 + .1);
	}
	return Vector(p, p, p) + sampler(h, r) * .5;
}

//----------------------------------------------------------------------
// Исходный вариант без акторов
void original() {
	FILE *out = fopen(FILE_NAME, "w");
	assert(out != NULL);
	fprintf(out, "P6 %d %d 255 ", WIDTH, HEIGHT);
	Vector g = !Vector(-6, -16, 0);
	Vector a = !(Vector(0, 0, 1) ^ g) * .002;
	Vector b = !(g ^ a) * .002;
	Vector c = (a + b) * -256 + g;
	for (int y = HEIGHT; y--;) {
		for (int x = WIDTH; x--;) {
			Vector p(13, 13, 13);
			for (int r = 64; r--;) {
				Vector t = a * (Random() - .5) * 99 + b * (Random() - .5) * 99;
				p = sampler(Vector(17, 16, 8) + t, !(t * -1 + (a * (Random() + x) + b * (y + Random()) + c) * 16)) * 3.5 + p;
			}
			p.print(out);
		}
	}
	fclose(out);
}

//----------------------------------------------------------------------
//Вариант с акторами
struct msg_t {
	int num;
	int x;
	int y;
	Vector result;
};

// Писатель (однопоточный)
class alignas(64) writer_t : public lite_worker_t {
	std::vector<lite_msg_t*> cache; // Кэш для пришедших не по порядку
	size_t next_write; // Следующий на запись
	FILE *out;
public:
	writer_t() {
		cache.assign(WIDTH * HEIGHT, (lite_msg_t*)NULL); // Заполнение кэша нулями
		next_write = 0; 
		out = fopen(FILE_NAME, "w");
		assert(out != NULL);
		fprintf(out, "P6 %d %d 255 ", WIDTH, HEIGHT);
	}

	~writer_t() {
		fclose(out);
	}

	void recv(lite_msg_t* msg) {
		msg_t* d = lite_msg_data<msg_t>(msg);
		assert(d != NULL);
		if(d->num != next_write) {
			// Пришло не по порядку, копия в кэш
			assert(d->num < WIDTH * HEIGHT);
			cache[d->num] = lite_msg_copy(msg);
		} else {
			// По порядку пришло, сразу в файл
			d->result.print(out);
			next_write++;
			// Запись из кэша сколько есть
			for (; next_write < WIDTH * HEIGHT && cache[next_write] != NULL; next_write++) {
				d = lite_msg_data<msg_t>(cache[next_write]);
				assert(d != NULL);
				d->result.print(out);
				lite_msg_erase(cache[next_write]); // Явное удаление т.к. была сделана копия
			}
		}
	}
};


// Считатель (потокобезопасный)
class alignas(64) worker_t : public lite_worker_t {
	Vector g = !Vector(-6, -16, 0);
	Vector a = !(Vector(0, 0, 1) ^ g) * .002;
	Vector b = !(g ^ a) * .002;
	Vector c = (a + b) * -256 + g;
	lite_actor_t* writer; // Актор писатель

public:
	worker_t() {
		writer = lite_actor_get("writer"); // Получение писателя по имени
		assert(writer != NULL);
	}

	// Расчет одного пикселя
	void calc(int x, int y, Vector& p) {
		p.init(13, 13, 13);
		for (int r = 64; r--;) {
			Vector t = a * (Random() - .5) * 99 + b * (Random() - .5) * 99;
			p = sampler(Vector(17, 16, 8) + t, !(t * -1 + (a * (Random() + x) + b * (y + Random()) + c) * 16)) * 3.5 + p;
		}
	}

	// Прием сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* d = lite_msg_data<msg_t>(msg); // Указатель на содержимое
		calc(d->x, d->y, d->result); // Расчет
		lite_thread_run(msg, writer); // Отправка
	}

};

// Запуск расчета
void actor_start(int threads) {
	// Создание акторов
	lite_actor_t* writer = lite_actor_create<writer_t>("writer");
	lite_actor_t* worker = lite_actor_create<worker_t>();
	// Глубина распараллеливания считателя
	lite_actor_parallel(threads, worker);
	// Общее ограничение CPU
	lite_resource_t* res = lite_resource_create("CPU", threads);
	writer->resource_set(res);
	worker->resource_set(res);
	// Создание сообщений
	int idx = 0; // номер сообщения
	for (int y = HEIGHT; y--;) {
		for (int x = WIDTH; x--;) {
			// Создание сообщения
			lite_msg_t* msg = lite_msg_create<msg_t>();
			// Заполнение
			msg_t* d = lite_msg_data<msg_t>(msg);
			d->num = idx++;
			d->x = x;
			d->y = y;
			// Отправка
			lite_thread_run(msg, worker);
		}
	}

}

int main(int argc, char **argv) {
	int threads = 0;
	if (argc > 1) {
		// Количество потоков
		for (char* p = argv[1]; *p != 0 && *p >= '0' && *p <= '9'; p++) threads = threads * 10 + *p - '0';
	} else {
		threads = 4;
	}
	printf("compile %s %s\n", __DATE__, __TIME__);
	lite_time_now(); // Начало отсчета времени
	if(threads == 0) { // Запуск оригинального кода
		printf("original code ...\n");
		original();
	} else { // запуск кода на lite_thread
		printf("lite_thread %d threads ...\n", threads);
		actor_start(threads);
		std::this_thread::sleep_for(std::chrono::milliseconds(100)); // для запуска потоков
		lite_thread_end(); // Ожидание окончания
	}
	printf("Time: %lld msec\n", lite_time_now());
	return 0;
}