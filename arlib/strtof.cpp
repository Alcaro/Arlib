#include "global.h"

// keeping this as a separate file allows .a optimization to delete the sscanf import

#if defined(__MINGW32__)
float strtof_arlib(const char * str, char** str_end)
{
	int n = 0;
	float ret;
	sscanf(str, "%f%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
double strtod_arlib(const char * str, char** str_end)
{
	int n = 0;
	double ret;
	sscanf(str, "%lf%n", &ret, &n);
	if (str_end) *str_end = (char*)str+n;
	return ret;
}
// gcc doesn't acknowledge scanf("%Lf") as legitimate
// I can agree that long double is creepy, I'll just leave it commented out until (if) I use ld
//long double strtold_arlib(const char * str, char** str_end)
//{
//	int n;
//	long double ret;
//	sscanf(str, "%Lf%n", &ret, &n);
//	if (str_end) *str_end = (char*)str+n;
//	return ret;
//}
#endif
