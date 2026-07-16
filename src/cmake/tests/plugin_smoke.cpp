#include <iostream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "No plugin paths were provided\n";
        return 2;
    }

    for (int index = 1; index < argc; ++index)
    {
#if defined(_WIN32)
        HMODULE module = LoadLibraryA(argv[index]);
        if (module == nullptr)
        {
            std::cerr << "LoadLibrary failed for " << argv[index]
                      << " with error " << GetLastError() << '\n';
            return 1;
        }
        FreeLibrary(module);
#else
        void* module = dlopen(argv[index], RTLD_NOW | RTLD_LOCAL);
        if (module == nullptr)
        {
            std::cerr << "dlopen failed for " << argv[index] << ": " << dlerror() << '\n';
            return 1;
        }
        dlclose(module);
#endif
    }
    return 0;
}
