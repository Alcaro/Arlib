// MIT License
//
// Copyright (c) 2016 Alfred Agrell
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

//The above license applies only to this file, not the entire Arlib.
#if 1
#include "wutf.h"
#include <stdio.h>

static void test816(const char* utf8, const char16_t* utf16_exp, int inlen=0, int outlen_e=0)
{
	if (!inlen)
	{
		while (utf8[inlen]) inlen++;
		inlen++;
	}
	if (!outlen_e)
	{
		while (utf16_exp[outlen_e]) outlen_e++;
		outlen_e++;
	}
	
	uint16_t utf16_act[128];
	int outlen_a = WuTF_utf8_to_utf16(false, utf8, inlen, utf16_act, 128);
	
	int outpos_a=0;
	int outpos_e=0;
	
	int outlen_a2 = WuTF_utf8_to_utf16(false, utf8, inlen, NULL, 0);
	if (outlen_a != outlen_a2) { printf("Expected length %i, got %i\n", outlen_a, outlen_a2); goto fail; }
	
	while (outpos_e < outlen_e && outpos_a < outlen_a)
	{
		uint16_t exp = utf16_exp[outpos_e++];
		while (exp==0xFFFD && utf16_exp[outpos_e]==0xFFFD) outpos_e++;
		uint16_t act = utf16_act[outpos_a++];
		while (act==0xFFFD && utf16_act[outpos_a]==0xFFFD) outpos_a++;
		if (exp!=act) goto fail;
	}
	if (outpos_e != outlen_e || outpos_a != outlen_a)
	{
	fail:
		puts(utf8);
		for (int i=0;i<outlen_e;i++) printf("%.4X ", utf16_exp[i]); puts("");
		for (int i=0;i<outlen_a;i++) printf("%.4X ", utf16_act[i]); puts("");
	}
}

static void test168(const char16_t* utf16, const char* utf8_exp, int inlen=0, int outlen_e=0)
{
	if (!inlen)
	{
		while (utf16[inlen]) inlen++;
		inlen++;
	}
	if (!outlen_e)
	{
		while (utf8_exp[outlen_e]) outlen_e++;
		outlen_e++;
	}
	
	char utf8_act[128];
	int outlen_a = WuTF_utf16_to_utf8(false, (const uint16_t*)utf16, inlen, utf8_act, 128);
	
	int outpos_a=0;
	int outpos_e=0;
	
	int outlen_a2 = WuTF_utf16_to_utf8(false, (const uint16_t*)utf16, inlen, NULL, 0);
	if (outlen_a != outlen_a2) { printf("Expected length %i, got %i\n", outlen_a, outlen_a2); goto fail; }
	
	while (outpos_e < outlen_e && outpos_a < outlen_a)
	{
		if (utf8_exp[outpos_e++] != utf8_act[outpos_a++]) goto fail;
	}
	if (outpos_e != outlen_e || outpos_a != outlen_a)
	{
	fail:
		for (int i=0;i<inlen;i++) printf("%.4X ", utf16[i]); puts("");
		for (int i=0;i<outlen_e;i++) printf("%.2X ", utf8_exp[i]&255); puts("");
		for (int i=0;i<outlen_a;i++) printf("%.2X ", utf8_act[i]&255); puts("");
	}
}

void WuTF_utf16_test()
{
	test816("a", u"a");
	test816("smörgåsräka", u"smörgåsräka");
	test816("♩♪♫♬", u"♩♪♫♬");
	test816("𝄞♩♪♫♬", u"𝄞♩♪♫♬");
	
	//http://www.cl.cam.ac.uk/~mgk25/ucs/examples/UTF-8-test.txt
	//bugs found in this implementation:
	//- I treated 0xF8 as 0xF0 rather than illegal
	//- the test for surrogate or not is <= 0xFFFF, not <
	
	//1  Some correct UTF-8 text
	test816("#\xce\xba\xe1\xbd\xb9\xcf\x83\xce\xbc\xce\xb5#", u"#\x03BA\x1F79\x03C3\x03BC\x03B5#");
	
	//2  Boundary condition test cases
	test816("#\0#",                       u"#\0#", 4,4);
	test816("#\xc2\x80#",                 u"#\x0080#");
	test816("#\xe0\xa0\x80#",             u"#\x0800#");
	test816("#\xf0\x90\x80\x80#",         u"#\xD800\xDC00#");
	test816("#\xf8\x88\x80\x80\x80#",     u"#\xFFFD#");
	test816("#\xfC\x84\x80\x80\x80\x80#", u"#\xFFFD#");
	
	test816("#\x7f#",                     u"#\x007F#");
	test816("#\xdf\xbf#",                 u"#\x07FF#");
	test816("#\xef\xbf\xbf#",             u"#\xFFFF#");
	test816("#\xf7\xbf\xbf\xbf#",         u"#\xFFFD#");
	test816("#\xfb\xbf\xbf\xbf\xbf#",     u"#\xFFFD#");
	test816("#\xfd\xbf\xbf\xbf\xbf\xbf#", u"#\xFFFD#");
	
	test816("#\xed\x9f\xbf#",     u"#\xD7FF#");
	test816("#\xee\x80\x80#",     u"#\xE000#");
	test816("#\xef\xbf\xbd#",     u"#\xFFFD#");
	test816("#\xf4\x8f\xbf\xbf#", u"#\xDBFF\xDFFF#");
	test816("#\xf4\x90\x80\x80#", u"#\xFFFD#");
	
	//3  Malformed sequences
	test816("#\x80#",                         u"#\xFFFD#");
	test816("#\xbf#",                         u"#\xFFFD#");
	
	test816("#\x80\xbf#",                     u"#\xFFFD#");
	test816("#\x80\xbf\x80#",                 u"#\xFFFD#");
	test816("#\x80\xbf\x80\xbf#",             u"#\xFFFD#");
	test816("#\x80\xbf\x80\xbf\x80#",         u"#\xFFFD#");
	test816("#\x80\xbf\x80\xbf\x80\xbf#",     u"#\xFFFD#");
	test816("#\x80\xbf\x80\xbf\x80\xbf\x80#", u"#\xFFFD#");
	
	test816("#\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f#", u"#\xFFFD#");
	test816("#\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f#", u"#\xFFFD#");
	test816("#\xa0\xa1\xa2\xa3\xa4\xa5\xa6\xa7\xa8\xa9\xaa\xab\xac\xad\xae\xaf#", u"#\xFFFD#");
	test816("#\xb0\xb1\xb2\xb3\xb4\xb5\xb6\xb7\xb8\xb9\xba\xbb\xbc\xbd\xbe\xbf#", u"#\xFFFD#");
	
	test816("#\xc0\x20\xc1\x20\xc2\x20\xc3\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xc4\x20\xc5\x20\xc6\x20\xc7\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xc8\x20\xc9\x20\xca\x20\xcb\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xcc\x20\xcd\x20\xce\x20\xcf\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xd0\x20\xd1\x20\xd2\x20\xd3\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xd4\x20\xd5\x20\xd6\x20\xd7\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xd8\x20\xd9\x20\xda\x20\xdb\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xdc\x20\xdd\x20\xde\x20\xdf\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	
	test816("#\xe0\x20\xe1\x20\xe2\x20\xe3\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xe4\x20\xe5\x20\xe6\x20\xe7\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xe8\x20\xe9\x20\xea\x20\xeb\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xec\x20\xed\x20\xee\x20\xef\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	
	test816("#\xf0\x20\xf1\x20\xf2\x20\xf3\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	test816("#\xf4\x20\xf5\x20\xf6\x20\xf7\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	
	test816("#\xf8\x20\xf9\x20\xfa\x20\xfb\x20#", u"#\xFFFD \xFFFD \xFFFD \xFFFD #");
	
	test816("#\xfc\x20\xfd\x20#",                 u"#\xFFFD \xFFFD #");
	
	test816("#\xc0#",                 u"#\xFFFD#");
	test816("#\xe0\x80#",             u"#\xFFFD#");
	test816("#\xf0\x80\x80#",         u"#\xFFFD#");
	test816("#\xf8\x80\x80\x80#",     u"#\xFFFD#");
	test816("#\xfc\x80\x80\x80\x80#", u"#\xFFFD#");
	
	test816("#\xdf#",                 u"#\xFFFD#");
	test816("#\xef\xbf#",             u"#\xFFFD#");
	test816("#\xf7\xbf\xbf#",         u"#\xFFFD#");
	test816("#\xfb\xbf\xbf\xbf#",     u"#\xFFFD#");
	test816("#\xfd\xbf\xbf\xbf\xbf#", u"#\xFFFD#");
	
	test816("#\xc0\xe0\x80\xf0\x80\x80\xf8\x80\x80\x80\xfc\x80\x80\x80\x80#", u"#\xFFFD#");
	test816("#\xdf\xef\xbf\xf7\xbf\xbf\xfb\xbf\xbf\xbf\xfd\xbf\xbf\xbf\xbf#", u"#\xFFFD#");
	
	test816("#\xfe#",             u"#\xFFFD#");
	test816("#\xff#",             u"#\xFFFD#");
	test816("#\xfe\xfe\xff\xff#", u"#\xFFFD#");
	
	//4  Overlong sequences
	test816("#\xc0\xaf#",                 u"#\xFFFD#");
	test816("#\xe0\x80\xaf#",             u"#\xFFFD#");
	test816("#\xf0\x80\x80\xaf#",         u"#\xFFFD#");
	test816("#\xf8\x80\x80\x80\xaf#",     u"#\xFFFD#");
	test816("#\xfc\x80\x80\x80\x80\xaf#", u"#\xFFFD#");

	test816("#\xc1\xbf#",                 u"#\xFFFD#");
	test816("#\xe0\x9f\xbf#",             u"#\xFFFD#");
	test816("#\xf0\x8f\xbf\xbf#",         u"#\xFFFD#");
	test816("#\xf8\x87\xbf\xbf\xbf#",     u"#\xFFFD#");
	test816("#\xfc\x83\xbf\xbf\xbf\xbf#", u"#\xFFFD#");

	test816("#\xc0\x80#",                 u"#\xFFFD#");
	test816("#\xe0\x80\x80#",             u"#\xFFFD#");
	test816("#\xf0\x80\x80\x80#",         u"#\xFFFD#");
	test816("#\xf8\x80\x80\x80\x80#",     u"#\xFFFD#");
	test816("#\xfc\x80\x80\x80\x80\x80#", u"#\xFFFD#");
	
	//5  Illegal code positions
	//these are valid WTF-8
	//test816("#\xed\xa0\x80#", u"#\xFFFD#");
	//test816("#\xed\xad\xbf#", u"#\xFFFD#");
	//test816("#\xed\xae\x80#", u"#\xFFFD#");
	//test816("#\xed\xaf\xbf#", u"#\xFFFD#");
	//test816("#\xed\xb0\x80#", u"#\xFFFD#");
	//test816("#\xed\xbe\x80#", u"#\xFFFD#");
	//test816("#\xed\xbf\xbf#", u"#\xFFFD#");
	
	//these are valid CESU-8
	//test816("#\xed\xa0\x80\xed\xb0\x80#", u"#\xFFFD#");
	//test816("#\xed\xa0\x80\xed\xbf\xbf#", u"#\xFFFD#");
	//test816("#\xed\xad\xbf\xed\xb0\x80#", u"#\xFFFD#");
	//test816("#\xed\xad\xbf\xed\xbf\xbf#", u"#\xFFFD#");
	//test816("#\xed\xae\x80\xed\xb0\x80#", u"#\xFFFD#");
	//test816("#\xed\xae\x80\xed\xbf\xbf#", u"#\xFFFD#");
	//test816("#\xed\xaf\xbf\xed\xb0\x80#", u"#\xFFFD#");
	//test816("#\xed\xaf\xbf\xed\xbf\xbf#", u"#\xFFFD#");
	
	test816("#\xef\xb7\x90\xef\xb7\x91\xef\xb7\x92\xef\xb7\x93#", u"#\xFDD0\xFDD1\xFDD2\xFDD3#");
	test816("#\xef\xb7\x94\xef\xb7\x95\xef\xb7\x96\xef\xb7\x97#", u"#\xFDD4\xFDD5\xFDD6\xFDD7#");
	test816("#\xef\xb7\x98\xef\xb7\x99\xef\xb7\x9a\xef\xb7\x9b#", u"#\xFDD8\xFDD9\xFDDA\xFDDB#");
	test816("#\xef\xb7\x9c\xef\xb7\x9d\xef\xb7\x9e\xef\xb7\x9f#", u"#\xFDDC\xFDDD\xFDDE\xFDDF#");
	test816("#\xef\xb7\xa0\xef\xb7\xa1\xef\xb7\xa2\xef\xb7\xa3#", u"#\xFDE0\xFDE1\xFDE2\xFDE3#");
	test816("#\xef\xb7\xa4\xef\xb7\xa5\xef\xb7\xa6\xef\xb7\xa7#", u"#\xFDE4\xFDE5\xFDE6\xFDE7#");
	test816("#\xef\xb7\xa8\xef\xb7\xa9\xef\xb7\xaa\xef\xb7\xab#", u"#\xFDE8\xFDE9\xFDEA\xFDEB#");
	test816("#\xef\xb7\xac\xef\xb7\xad\xef\xb7\xae\xef\xb7\xaf#", u"#\xFDEC\xFDED\xFDEE\xFDEF#");
	
	test816("#\xf0\x9f\xbf\xbe\xf0\x9f\xbf\xbf\xf0\xaf\xbf\xbe#", u"#\xD83F\xDFFE\xD83F\xDFFF\xD87F\xDFFE#");
	test816("#\xf0\xaf\xbf\xbf\xf0\xbf\xbf\xbe\xf0\xbf\xbf\xbf#", u"#\xD87F\xDFFF\xD8BF\xDFFE\xD8BF\xDFFF#");
	test816("#\xf1\x8f\xbf\xbe\xf1\x8f\xbf\xbf\xf1\x9f\xbf\xbe#", u"#\xD8FF\xDFFE\xD8FF\xDFFF\xD93F\xDFFE#");
	test816("#\xf1\x9f\xbf\xbf\xf1\xaf\xbf\xbe\xf1\xaf\xbf\xbf#", u"#\xD93F\xDFFF\xD97F\xDFFE\xD97F\xDFFF#");
	test816("#\xf1\xbf\xbf\xbe\xf1\xbf\xbf\xbf\xf2\x8f\xbf\xbe#", u"#\xD9BF\xDFFE\xD9BF\xDFFF\xD9FF\xDFFE#");
	test816("#\xf2\x8f\xbf\xbf\xf2\x9f\xbf\xbe\xf2\x9f\xbf\xbf#", u"#\xD9FF\xDFFF\xDA3F\xDFFE\xDA3F\xDFFF#");
	test816("#\xf2\xaf\xbf\xbe\xf2\xaf\xbf\xbf\xf2\xbf\xbf\xbe#", u"#\xDA7F\xDFFE\xDA7F\xDFFF\xDABF\xDFFE#");
	test816("#\xf2\xbf\xbf\xbf\xf3\x8f\xbf\xbe\xf3\x8f\xbf\xbf#", u"#\xDABF\xDFFF\xDAFF\xDFFE\xDAFF\xDFFF#");
	test816("#\xf3\x9f\xbf\xbe\xf3\x9f\xbf\xbf\xf3\xaf\xbf\xbe#", u"#\xDB3F\xDFFE\xDB3F\xDFFF\xDB7F\xDFFE#");
	test816("#\xf3\xaf\xbf\xbf\xf3\xbf\xbf\xbe\xf3\xbf\xbf\xbf#", u"#\xDB7F\xDFFF\xDBBF\xDFFE\xDBBF\xDFFF#");
	test816("#\xf4\x8f\xbf\xbe\xf4\x8f\xbf\xbf#",                 u"#\xDBFF\xDFFE\xDBFF\xDFFF#");
	
	
	//////////////////////////////////////////////////////////////////////////////////////////////////
	//some of these are the above backwards, some are other tricks
	test168(u"a", "a");
	test168(u"smörgåsräka", "smörgåsräka");
	test168(u"♩♪♫♬", "♩♪♫♬");
	test168(u"𝄞♩♪♫♬", "𝄞♩♪♫♬");
	
	test168(u"#\x0000#",       "#\x00#", 4,4);
	test168(u"#\x0080#",       "#\xc2\x80#");
	test168(u"#\x0800#",       "#\xe0\xa0\x80#");
	test168(u"#\xD800\xDC00#", "#\xf0\x90\x80\x80#");
	
	test168(u"#\x007F#",       "#\x7f#");
	test168(u"#\x07FF#",       "#\xdf\xbf#");
	test168(u"#\xFFFF#",       "#\xef\xbf\xbf#");
	
	test168(u"#\xD7FF#",       "#\xed\x9f\xbf#");
	test168(u"#\xE000#",       "#\xee\x80\x80#");
	test168(u"#\xFFFD#",       "#\xef\xbf\xbd#");
	test168(u"#\xDBFF\xDFFF#", "#\xf4\x8f\xbf\xbf#");
	
	test168(u"#\xD800#",       "#\xed\xa0\x80#");
	test168(u"#\xDBFF#",       "#\xed\xaf\xbf#");
	test168(u"#\xDC00#",       "#\xed\xb0\x80#");
	test168(u"#\xDFFF#",       "#\xed\xbf\xbf#");
}
#endif
