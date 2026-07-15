#include <iostream>

#include "../../Includes/IDataWriter.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace
{
#if defined(_WIN32)
void* getSymbol(HMODULE module, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(module, name));
}
#else
void* getSymbol(void* module, const char* name)
{
    return dlsym(module, name);
}
#endif

bool checkWriterLifecycle(
#if defined(_WIN32)
    HMODULE module,
#else
    void* module,
#endif
    const char* path)
{
    auto creator = reinterpret_cast<FuncCreateWriter>(getSymbol(module, "createWriter"));
    auto remover = reinterpret_cast<FuncDeleteWriter>(getSymbol(module, "deleteWriter"));
    if ((creator == nullptr) != (remover == nullptr))
    {
        std::cerr << "Writer factory symbols are incomplete in " << path << '\n';
        return false;
    }
    if (creator == nullptr)
        return true;

    wtp::IDataWriter* writer = creator();
    if (writer == nullptr)
    {
        std::cerr << "createWriter returned null for " << path << '\n';
        return false;
    }
    remover(writer);
    if (writer != nullptr)
    {
        std::cerr << "deleteWriter did not clear the pointer for " << path << '\n';
        return false;
    }
    return true;
}
}

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
        if (!checkWriterLifecycle(module, argv[index]))
        {
            FreeLibrary(module);
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
        if (!checkWriterLifecycle(module, argv[index]))
        {
            dlclose(module);
            return 1;
        }
        dlclose(module);
#endif
    }
    return 0;
}
