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

#include "wutf.h"
#include<stdio.h>

static int decode(uint8_t head, const uint8_t* * ptr, const uint8_t* end)
{
	int numtrail = ((head&0xC0)==0xC0) + ((head&0xE0)==0xE0) + ((head&0xF0)==0xF0);
	const int minforlen[] = { 0x7FFFFFFF, 0x80, 0x800, 0x10000 };
	
	if (*ptr + numtrail > end) return -1;
	
	int codepoint;
	codepoint = (head & (0x3F>>numtrail));
	for (int i=1;i<=3;i++)
	{
		if (numtrail>=i)
		{
			if ((**ptr & 0xC0) != 0x80) return -1;
			codepoint = (codepoint<<6) | ((*(*ptr)++) & 0x3F);
		}
	}
	
	if (codepoint < minforlen[numtrail]) return -1;
	
	return codepoint;
}

//doesn't throw errors on invalid input
static int UtfAsWideLen(const uint8_t* ptr, const uint8_t* end)
{
	int ret = 0;
	const uint8_t* at = ptr;
	while (at<end)
	{
		//TODO: don't just implement strlen
		at++;
		ret++;
	}
	return ret;
}

int WuTF_utf8_to_utf16(bool strict, const char* utf8, int utf8_len, uint16_t* utf16, int utf16_len)
{
	//I could run a bunch of special cases depending on whether cbMultiByte<0, etc,
	//but there's no need. I'll optimize it if it ends up a bottleneck.
	
	const uint8_t* iat = (const uint8_t*)utf8;
	const uint8_t* iend;
	if (utf8_len >= 0)
	{
		iend = iat + utf8_len;
	}
	else
	{
		iend = iat;
		while (*iend) iend++;
		iend++; // go behind the NUL
	}
	
	if (utf16_len == 0)
	{
		return UtfAsWideLen(iat, iend);
	}
	
	uint16_t* oat = utf16;
	uint16_t* oend = oat + utf16_len;
	
	while (iat < iend && oat < oend)
	{
		uint8_t head = *iat++;
		if (head <= 0x7F)
		{
			*oat++ = head;
			continue;
		}
		
		int codepoint = decode(head, &iat, iend);
		if (codepoint<0)
		{
		fail:
			if (strict) return -1;
			*oat++ = 0xFFFD;
		}
		else if (codepoint <= 0xFFFF)
		{
			*oat++ = codepoint;
		}
		else
		{
			if (codepoint > 0x10FFFF) goto fail;
			
			// for heads >= 0xF0, the 08 bit is ignored
			// F0 means 4+ bytes, which conveniently must be outside the BMP
			// so I can put the check here, on the cold path
			if (head >= 0xF8) goto fail;
			
			codepoint -= 0x10000;
			*oat++ = 0xD800 | (codepoint>>10);
			*oat++ = 0xDC00 | (codepoint&0x3FF);
		}
	}
	
	return oat - utf16;
}

