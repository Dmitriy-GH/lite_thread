/*
������ DLL ���������� � �.�. ��� WinXP 

����������� ���������� ����� ��� ������ � ���. 
����� ������� ��������� � ���� "test.log"

fntest(const char* str) ���������� str ������.
*/

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include "test_dll.h"

#define LT_XP_DLL // ��� ������ DLL � WinXP, ��� LT_XP_DLL �������� �������, �� ������ � Win7+
#include "../lite_thread.h"

//-------------------------------------------------------------------------
// ����� ��� ������ � ���
class logger_t : public lite_actor_t {
	FILE* f;

	void write(const char* str) {
		if(f != NULL) {
			fputs(str, f);
			fputs("\n", f);
			fflush(f);
		}
	}

	void recv(lite_msg_t* msg) override {
		lite_msg_log_t* m = dynamic_cast<lite_msg_log_t*>(msg);
		assert(m != NULL);
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

//-------------------------------------------------------------------------

TEST_API void fntest(const char* str) {
	static bool is_init;
	if(!is_init) { 
		is_init = true;
		// ������ ����������� ������� �� �����������
		logger_t* l = new logger_t();
		l->name_set("log");
	}
	// ����� � ���
	lite_log(0, str);
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD  ul_reason_for_call, LPVOID lpReserved) {
	DisableThreadLibraryCalls(hModule);
	switch (ul_reason_for_call)	{
		case DLL_PROCESS_ATTACH:
			break;
		case DLL_THREAD_ATTACH:
			break;
		case DLL_THREAD_DETACH:
			break;
		case DLL_PROCESS_DETACH:
			lite_thread_end();
			break;
	}
	return TRUE;
}


