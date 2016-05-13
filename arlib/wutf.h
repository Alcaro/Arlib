#pragma once

#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
//Switches the code page of all Windows ANSI and libc functions (for example CreateFileA() and fopen()) to UTF-8.
//WARNING: Performs ugly shenanigans that immediately voids your warranty.
//WARNING: Never ever put this in a DLL. If it's unloaded, you'll get segfaults really fast.
//Don't do it even if the DLL is present for the entire program duration. It will be unloaded during program shutdown.
void WuTF_enable();
//Converts argc/argv to UTF-8.
void WuTF_args(int* argc, char** * argv);
#ifdef __cplusplus
}
#endif

#else
//Other OSes already use UTF-8.
static inline void WuTF_enable() {}
static inline void WuTF_args(int* argc, char** * argv) {}
#endif

//This one just combines the above.
static inline void WuTF_enable_args(int* argc, char** * argv) { WuTF_enable(); WuTF_args(argc, argv); }
