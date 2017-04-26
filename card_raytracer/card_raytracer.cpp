﻿/**
Исходный тест https://github.com/Mark-Kovalyov/CardRaytracerBenchmark/blob/master/cpp/card-raytracer.cpp

Пример преобразования однопоточного кода генератора картинки img.ppm размером 512*512
img.ppm можно посмотреть каким-нибудь просмотрщиком изображений, например IrfanView

Исходный вариант решения (original()) открывает файл, в двух вложенных циклах (высота, ширина) последовательно
обсчитывает каждый пиксель изображения и сохраняет в файл. 

По модели акторов создается три актора:
1. Считатель. Обсчитывает один пиксель. Код Считателя потокобезопасный, т.к. не имеет меняющегося окружения, 
   поэтому его можно запускать параллельно.
2. Упорядочиватель. Однопоточный. Восстанавливает порядок следования сообщений.
3. Писатель. Однопоточный. Пишет результат в файл.

В начале работы (actor_start(int threads)) создается 260 тыс. сообщений, заданий на обсчет каждой точки и 
отправляются Считателю. По окончанию обсчета точки Считатель отправляет результат Упорядочивателю, который
кэширует пришедшие не по порядку сообщения и отправляет Писателю сообщения в соответствии с изначальным 
порядком.


-----------------------------------------------------------------------------------------------------------
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
#include "../lite_thread_util.h"

#define WIDTH  512
#define HEIGHT 512

#define FILE_NAME "img.ppm"



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
struct msg_t : public lite_msg_t {
	size_t idx;
	int x;
	int y;
	Vector result;
};

// Писатель (однопоточный)
class writer_t : public lite_actor_t {
	FILE *out;
public:
	writer_t() {
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
		out = fopen(FILE_NAME, "w");
		assert(out != NULL);
		fprintf(out, "P6 %d %d 255 ", WIDTH, HEIGHT);
	}

	~writer_t() {
		fclose(out);
	}

	// Обработка сообщения
	void recv(lite_msg_t* msg) override {
		msg_t* m = static_cast<msg_t*>(msg);
		assert(m != NULL);
		m->result.print(out);
	}
};


// Считатель (потокобезопасный)
class worker_t : public lite_actor_t {
	Vector g = !Vector(-6, -16, 0);
	Vector a = !(Vector(0, 0, 1) ^ g) * .002;
	Vector b = !(g ^ a) * .002;
	Vector c = (a + b) * -256 + g;
	lite_actor_t* order; // Упорядочиватель

public:
	worker_t() {
		order = lite_actor_get("order"); // Получение упорядочивателя по имени
		assert(order != NULL);
		type_add(lite_msg_type<msg_t>()); // Разрешение принимать сообщение типа msg_t
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
		msg_t* m = static_cast<msg_t*>(msg); // Указатель на содержимое
		assert(m != NULL);
		calc(m->x, m->y, m->result); // Расчет
		order->run(msg); // Отправка
	}

};

// Запуск расчета
void actor_start(int threads) {
	// Создание акторов
	// Писатель
	writer_t* writer = new writer_t;
	writer->name_set("writer");

	// Упорядочиватель (из lite_thread_util.h)
	lite_order_t<msg_t>* order = new lite_order_t<msg_t>("writer");
	order->name_set("order");

	// Считатель 
	worker_t* worker = new worker_t;
	worker->name_set("worker");
	worker->parallel_set(threads);

	// Ограничение количества потоков
	lite_thread_max(threads);

	// Создание сообщений
	size_t idx = 0; // номер сообщения
	for (int y = HEIGHT; y--;) {
		for (int x = WIDTH; x--;) {
			// Создание сообщения
			msg_t* msg = new msg_t;
			// Заполнение
			msg->idx = idx++;
			msg->x = x;
			msg->y = y;
			// Отправка
			worker->run(msg);
		}
	}

	lite_thread_end(); // Ожидание окончания расчета
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
	}
	printf("Time: %lld msec\n", lite_time_now());
	return 0;
}