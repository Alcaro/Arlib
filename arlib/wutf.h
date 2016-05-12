#pragma once

#ifdef _WIN32
//Switches the code page of all Windows ANSI and libc functions (for example CreateFileA() and fopen()) to UTF-8.
//WARNING: Never ever put this in a DLL. If it's unloaded, you'll get segfaults really fast.
//Don't do it if the DLL is present for the entire program duration. It will be unloaded during program shutdown.
extern "C" void WUTfEnable();
//Converts argc/argv to UTF-8.
extern "C" void WUTfArgs(int* argc, char** * argv);
#else
//Other OSes already use UTF-8.
static inline void WUTfEnable() {}
static inline void WUTfArgs(int* argc, char** * argv) {}
#endif

//This one just combines the above.
static inline void WUTfEnableArgs(int* argc, char** * argv) { WUTfEnable(); WUTfArgs(argc, argv); }
