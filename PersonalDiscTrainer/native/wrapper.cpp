// Echo's existing script loader calls this export. Delegate the complete ABI
// to the preserved script, then extend only its update/destroy callbacks.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#pragma comment(lib,"user32.lib")
#ifndef SCRIPT_NAME
#error SCRIPT_NAME required
#endif
extern "C" __declspec(dllimport) void BindTrainer(unsigned, void*, HMODULE);
extern "C" __declspec(dllexport) void setup_bindings(void* table) {
    wchar_t file[32768];
    GetModuleFileNameW(nullptr,file,32768);
    std::wstring path(file);
    path.resize(path.find_last_of(L"\\/")+1);
    path+=L"scripts\\" SCRIPT_NAME L".trainer-original.dll";
    // Keep the original loaded for the lifetime of its delegated callbacks.
    static HMODULE original=LoadLibraryExW(path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
    auto setup=reinterpret_cast<void(*)(void*)>(GetProcAddress(original,"setup_bindings"));
    if (!setup) {
        MessageBoxW(nullptr,L"The original Echo script is missing. Run the trainer installer Repair/Restore before launching Echo.",L"Echo tablet trainer",MB_ICONERROR);
        TerminateProcess(GetCurrentProcess(),ERROR_MOD_NOT_FOUND);
        return;
    }
    setup(table);
    BindTrainer(SCRIPT_KIND,table,original);
}
