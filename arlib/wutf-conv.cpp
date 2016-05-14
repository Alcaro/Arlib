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
#include<stdio.h>

#include "wutf.h"

//WARNING: Treats 0xF8..FF as F0..F7. The caller is expected to validate this if the codepoint is
//outside the BMP.
static int decode(uint8_t head, const uint8_t* * ptr, const uint8_t* end)
{
	int numtrail = ((head&0xC0)==0xC0) + ((head&0xE0)==0xE0) + ((head&0xF0)==0xF0);
	const int minforlen[] = { 0x7FFFFFFF, 0x80, 0x800, 0x10000 };
	
	if (*ptr + numtrail > end) return -1;
	
	int codepoint = (head & (0x3F>>numtrail));
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

static int utf8_to_utf16_len(bool strict, const uint8_t* ptr, const uint8_t* end)
{
	int ret = 0;
	const uint8_t* at = ptr;
	
	while (at < end)
	{
		uint8_t head = *at++;
		if (head <= 0x7F)
		{
			ret++;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &at, end);
		ret++;
		if (codepoint>0x10FFFF || head>=0xF8)
		{
			if (strict) return -1;
		}
		else if (codepoint>=0x10000) ret++; // surrogate
	}
	return ret;
}

static const uint8_t* utf8_end(const uint8_t* utf8, int utf8_len)
{
	if (utf8_len >= 0)
	{
		return utf8 + utf8_len;
	}
	else
	{
		while (*utf8) utf8++;
		utf8++; // go behind the NUL
		return utf8;
	}
}

int WuTF_utf8_to_utf16(bool strict, const char* utf8, int utf8_len, uint16_t* utf16, int utf16_len)
{
	//I could run a bunch of special cases depending on whether cbMultiByte<0, etc,
	//but there's no need. I'll optimize it if it ends up a bottleneck.
	
	const uint8_t* iat = (const uint8_t*)utf8;
	const uint8_t* iend = utf8_end(iat, utf8_len);
	
	if (utf16_len == 0)
	{
		return utf8_to_utf16_len(strict, iat, iend);
	}
	
	uint16_t* oat = utf16;
	uint16_t* oend = oat + utf16_len;
	
	while (iat < iend && oat < oend)
	{
		if (oat+1 > oend) return -2;
		
		uint8_t head = *iat++;
		if (head <= 0x7F)
		{
			*oat++ = head;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &iat, iend); // -1 -> 0xFFFF
		if (codepoint <= 0xFFFF)
		{
			*oat++ = codepoint;
		}
		else
		{
			// for heads >= 0xF0, the 08 bit is ignored
			// F0 means 4+ bytes, which conveniently must be outside the BMP
			// so I can put the check here, on the cold path
			if (head >= 0xF8 || codepoint > 0x10FFFF)
			{
				if (strict) return -1;
				*oat++ = 0xFFFD;
				continue;
			}
			
			if (oat+2 > oend) return -2;
			codepoint -= 0x10000;
			*oat++ = 0xD800 | (codepoint>>10);
			*oat++ = 0xDC00 | (codepoint&0x3FF);
		}
	}
	
	return oat - utf16;
}



static int utf8_to_utf32_len(bool strict, const uint8_t* ptr, const uint8_t* end)
{
	int ret = 0;
	const uint8_t* at = ptr;
	
	while (at < end)
	{
		uint8_t head = *at++;
		if (head <= 0x7F)
		{
			ret++;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &at, end);
		ret++;
		if (codepoint>0x10FFFF || head>=0xF8)
		{
			if (strict) return -1;
		}
		//identical to utf16_len, just discard the surrogate check
	}
	return ret;
}

int WuTF_utf8_to_utf32(bool strict, const char* utf8, int utf8_len, uint32_t* utf32, int utf32_len)
{
	const uint8_t* iat = (const uint8_t*)utf8;
	const uint8_t* iend = utf8_end(iat, utf8_len);
	
	if (utf32_len == 0)
	{
		return utf8_to_utf32_len(strict, iat, iend);
	}
	
	uint32_t* oat = utf32;
	uint32_t* oend = oat + utf32_len;
	
	while (iat < iend)
	{
		if (oat+1 > oend) return -2;
		
		uint8_t head = *iat++;
		if (head <= 0x7F)
		{
			*oat++ = head;
			continue;
		}
		
		uint32_t codepoint = (uint32_t)decode(head, &iat, iend); // -1 -> 0xFFFF
		
		if (head >= 0xF8 || codepoint > 0x10FFFF)
		{
			if (strict) return -1;
			*oat++ = 0xFFFD;
			continue;
		}
		
		*oat++ = codepoint;
	}
	
	return oat - utf32;
}





static int utf16_to_utf8_len(bool strict, const uint16_t* ptr, const uint16_t* end)
{
	int ret = 0;
	const uint16_t* at = ptr;
	
	while (at < end)
	{
		uint16_t head = *at++;
		ret++;
		if (head >= 0x80) ret++;
		if (head >= 0x0800)
		{
			ret++;
			if (head>=0xD800 && head<=0xDBFF &&
			    at < end && *at >= 0xDC00 && *at <= 0xDFFF)
			{
				at++;
				ret++;
				continue;
			}
		}
	}
	return ret;
}

static const uint16_t* utf16_end(const uint16_t* utf16, int utf16_len)
{
	if (utf16_len >= 0)
	{
		return utf16 + utf16_len;
	}
	else
	{
		while (*utf16) utf16++;
		utf16++; // go behind the NUL
		return utf16;
	}
}

int WuTF_utf16_to_utf8(bool strict, const uint16_t* utf16, int utf16_len, char* utf8, int utf8_len)
{
	const uint16_t* iat = utf16;
	const uint16_t* iend = utf16_end(iat, utf16_len);
	
	if (utf8_len == 0)
	{
		return utf16_to_utf8_len(strict, iat, iend);
	}
	
	uint8_t* oat = (uint8_t*)utf8;
	uint8_t* oend = oat + utf8_len;
	
	while (iat < iend)
	{
		uint16_t head = *iat++;
		if (head <= 0x7F)
		{
			if (oat+1 > oend) return -2;
			*oat++ = head;
		}
		else if (head <= 0x07FF)
		{
			if (oat+2 > oend) return -2;
			*oat++ = (((head>> 6)     )|0xC0);
			*oat++ = (((head    )&0x3F)|0x80);
		}
		else
		{
			if (head>=0xD800 && head<=0xDBFF && iat < iend)
			{
				uint16_t tail = *iat;
				if (tail >= 0xDC00 && tail <= 0xDFFF)
				{
					iat++;
					if (oat+4 > oend) return -2;
					uint32_t codepoint = 0x10000+((head&0x03FF)<<10)+(tail&0x03FF);
					*oat++ = (((codepoint>>18)&0x07)|0xF0);
					*oat++ = (((codepoint>>12)&0x3F)|0x80);
					*oat++ = (((codepoint>>6 )&0x3F)|0x80);
					*oat++ = (((codepoint    )&0x3F)|0x80);
					continue;
				}
			}
			if (oat+3 > oend) return -2;
			*oat++ = (((head>>12)&0x0F)|0xE0);
			*oat++ = (((head>>6 )&0x3F)|0x80);
			*oat++ = (((head    )&0x3F)|0x80);
		}
	}
	
	return oat - (uint8_t*)utf8;
}



static int utf32_to_utf8_len(bool strict, const uint32_t* ptr, const uint32_t* end)
{
	int ret = 0;
	const uint32_t* at = ptr;
	
	while (at < end)
	{
		uint32_t head = *at++;
		ret++;
		if (head >= 0x80) ret++;
		if (head >= 0x0800) ret++;
		if (head >= 0x10000) ret++;
		if (head > 0x10FFFF) return -1;
	}
	return ret;
}

static const uint32_t* utf32_end(const uint32_t* utf32, int utf32_len)
{
	if (utf32_len >= 0)
	{
		return utf32 + utf32_len;
	}
	else
	{
		while (*utf32) utf32++;
		utf32++; // go behind the NUL
		return utf32;
	}
}

int WuTF_utf32_to_utf8(bool strict, const uint32_t* utf32, int utf32_len, char* utf8, int utf8_len)
{
	const uint32_t* iat = utf32;
	const uint32_t* iend = utf32_end(iat, utf32_len);
	
	if (utf8_len == 0)
	{
		return utf32_to_utf8_len(strict, iat, iend);
	}
	
	uint8_t* oat = (uint8_t*)utf8;
	uint8_t* oend = oat + utf8_len;
	
	while (iat < iend)
	{
		uint32_t head = *iat++;
		if (head <= 0x7F)
		{
			if (oat+1 > oend) return -2;
			*oat++ = head;
		}
		else if (head <= 0x07FF)
		{
			if (oat+2 > oend) return -2;
			*oat++ = (((head>> 6)     )|0xC0);
			*oat++ = (((head    )&0x3F)|0x80);
		}
		else if (head <= 0xFFFF)
		{
			if (oat+3 > oend) return -2;
			*oat++ = (((head>>12)&0x0F)|0xE0);
			*oat++ = (((head>>6 )&0x3F)|0x80);
			*oat++ = (((head    )&0x3F)|0x80);
		}
		else if (head <= 0x10FFFF)
		{
			if (oat+4 > oend) return -2;
			*oat++ = (((head>>18)&0x07)|0xF0);
			*oat++ = (((head>>12)&0x3F)|0x80);
			*oat++ = (((head>>6 )&0x3F)|0x80);
			*oat++ = (((head    )&0x3F)|0x80);
		}
		else return -1;
	}
	
	return oat - (uint8_t*)utf8;
}
