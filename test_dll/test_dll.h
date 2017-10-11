#pragma once
#ifdef TEST_DLL_EXPORTS
#define TEST_API __declspec(dllexport)
#else
#define TEST_API __declspec(dllimport)
#endif

extern "C" TEST_API void fntest(const char* str);