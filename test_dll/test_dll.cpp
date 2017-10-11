// test_dll.cpp : Defines the exported functions for the DLL application.
//
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "test_dll.h"
#define LT_XP_DLL
#include "../lite_thread.h"

class logger_t : public lite_actor_t {
	FILE* f;

	void write(const char* str) {
		if(f != NULL) {
			fputs(str, f);
			fputs("\n", f);
		}
	}

	void recv(lite_msg_t* msg) override {
		lite_msg_log_t* m = dynamic_cast<lite_msg_log_t*>(msg);
		write(m->data.c_str());
	}

public:
	logger_t() {
		f = fopen("test.log", "ab");
		write("start");
		type_add(lite_msg_type<lite_msg_log_t>());
	}

	~logger_t() {
		write("stop");
		if(f != NULL) {
			fclose(f);
		}
	}
};

void start() {
	MessageBox(0, "1", "1", 0);
	logger_t* l = new logger_t();
	MessageBox(0, "1", "2", 0);
	l->name_set("log");
	MessageBox(0, "1", "3", 0);
}

void stop() {
	lite_thread_end();
}

TEST_API void fntest(const char* str) {
	lite_log(0, str);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		start();
		break;
	case DLL_THREAD_ATTACH:
		break;
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		stop();
		break;
	}
	return TRUE;
}


