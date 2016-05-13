#pragma once

#ifdef _WIN32
#ifdef __cplusplus
extern "C" {
#endif
//This function switches the code page of all Windows ANSI and libc functions
// (for example CreateFileA() and fopen()) to UTF-8.

//Limitations:
//- IMMEDIATELY VOIDS YOUR WARRANTY
//- Possibly makes antivirus software panic
//- Will crash if this code is in a DLL that's unloaded, possibly including program shutdown.
//- Disables support for non-UTF8 code pages in MultiByteToWideChar and WideCharToMultiByte and
//    treats them as UTF-8, even if explicitly requested otherwise.
//- Console input and output remains ANSI. Consoles are very strangely implemented in Windows;
//    judging by struct CHAR_INFO in WriteConsoleOutput's arguments, the consoles don't support
//    UTF-16, but only UCS-2.
//- Did I mention it voids your warranty?
void WuTF_enable();

//Converts argc/argv to UTF-8. (Unlike the above, it uses zero ugly hacks.)
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
