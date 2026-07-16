#if defined(_WIN32)
#define WT_DLLHELPER_FIXTURE_API __declspec(dllexport)
#else
#define WT_DLLHELPER_FIXTURE_API __attribute__((visibility("default")))
#endif

extern "C" WT_DLLHELPER_FIXTURE_API int wt_dllhelper_fixture_value()
{
	return 20260707;
}
