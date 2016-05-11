#pragma once

#ifdef _WIN32
//Switches the code page of all Windows ANSI and libc functions (for example CreateFileA() and fopen()) to UTF-8.
extern "C" void WUTfEenable();
//Converts argc/argv to UTF-8.
extern "C" void WUTfArgs(int* argc, char** * argv);
#else
//Other OSes are already UTF-8.
static inline void WUTfEnable() {}
static inline void WUTfArgs(int* argc, char** * argv) {}
#endif

//This one just combines the above.
static inline void WUTfEnableArgs(int* argc, char** * argv) { WUTfEnable(); WUTfArgs(argc, argv); }
