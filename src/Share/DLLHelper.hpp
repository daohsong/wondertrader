/*!
 * \file DLLHelper.hpp
 * \project	WonderTrader
 *
 * \author Wesley
 * \date 2020/03/30
 *
 * \brief 动态库辅助类,主要是把跨平台的差异封装起来,方便调用
 */
#pragma once

#include "ModuleNameCompat.hpp"

#include <string>
#include <type_traits>

#ifdef _WIN32
#include <windows.h>
typedef HMODULE		DllHandle;
typedef void*		ProcHandle;
#else
#include <dlfcn.h>
typedef void*		DllHandle;
typedef void*		ProcHandle;
#endif

class DLLHelper
{
public:
	static constexpr const char* module_suffix()
	{
#ifdef _WIN32
		return ".dll";
#elif defined(__APPLE__)
		return ".dylib";
#else
		return ".so";
#endif
	}

	static DllHandle load_library(const char* filename)
	{
		try
		{
			clear_last_error();
#ifdef _WIN32
			DllHandle ret = ::LoadLibraryA(filename);
			if (ret == NULL)
				set_last_error_from_system();
			return ret;
#else
			DllHandle ret = dlopen(filename, RTLD_NOW);
			if (ret == NULL)
			{
				const char* err = dlerror();
				if (err != NULL)
					set_last_error(err);
			}
			return ret;
#endif
		}
		catch (...)
		{
			set_last_error("unexpected exception while loading dynamic library");
			return NULL;
		}
	}

	static void free_library(DllHandle handle)
	{
		if (NULL == handle)
			return;

#ifdef _WIN32
		::FreeLibrary(handle);
#else
		dlclose(handle);
#endif
	}

	static ProcHandle get_symbol(DllHandle handle, const char* name)
	{
		if (NULL == handle)
		{
			set_last_error("invalid library handle");
			return NULL;
		}
		if (name == NULL)
		{
			set_last_error("invalid symbol name");
			return NULL;
		}

#ifdef _WIN32
		clear_last_error();
		ProcHandle ret = reinterpret_cast<ProcHandle>(::GetProcAddress(handle, name));
		if (ret == NULL)
			set_last_error_from_system();
		return ret;
#else
		(void)dlerror();
		ProcHandle ret = dlsym(handle, name);
		const char* err = dlerror();
		if (err != NULL)
			set_last_error(err);
		else
			clear_last_error();
		return ret;
#endif
	}

	template <class Fn>
	static Fn get_typed_symbol(DllHandle handle, const char* name)
	{
		static_assert(std::is_pointer<Fn>::value
			&& std::is_function<typename std::remove_pointer<Fn>::type>::value,
			"Fn must be a function pointer type");
		return reinterpret_cast<Fn>(get_symbol(handle, name));
	}

	static const std::string& last_error()
	{
		return last_error_storage();
	}

	static std::string wrap_module(const char* name, const char* unixPrefix = "lib")
	{
#ifdef _WIN32
		return ModuleNameCompat::wrap_module_name(name, "", module_suffix(), false, true);
#else
		return ModuleNameCompat::wrap_module_name(name, unixPrefix, module_suffix(), true);
#endif
	}

private:
	static void clear_last_error()
	{
		last_error_storage().clear();
	}

	static void set_last_error(const char* error)
	{
		last_error_storage() = error == NULL ? std::string() : std::string(error);
	}

#ifdef _WIN32
	static void set_last_error_from_system()
	{
		last_error_storage() = "Windows error " + std::to_string(::GetLastError());
	}
#endif

	static std::string& last_error_storage()
	{
		static thread_local std::string error;
		return error;
	}
};
