#include "image.h"

static inline void image_insert_noov(image& target, int32_t x, int32_t y, const image& source);
//checks if the source fits in the target, and if not, crops it; then calls the above
void image::insert(int32_t x, int32_t y, const image& other)
{
	if (UNLIKELY(x < 0 || y < 0 || x+other.width > width || y+other.height > height))
	{
		struct image part;
		part.init_ref(other);
		if (x < 0)
		{
			part.pixels = (uint8_t*)part.pixels + (-x)*byteperpix(part.fmt);
			if ((uint32_t)-x >= part.width) return;
			part.width -= -x;
			x = 0;
		}
		if (y < 0)
		{
			part.pixels = (uint8_t*)part.pixels + (-y)*part.stride;
			if ((uint32_t)-y >= part.height) return;
			part.height -= -y;
			y = 0;
		}
		if (x+part.width > width)
		{
			if ((uint32_t)x >= width) return;
			part.width = width-x;
		}
		if (y+part.height > height)
		{
			if ((uint32_t)y >= height) return;
			part.height = height-y;
		}
		image_insert_noov(*this, x, y, part);
	}
	else
	{
		image_insert_noov(*this, x, y, other);
	}
}

template<bool composite, bool checkalpha, uint32_t newalpha>
static inline void image_insert_noov_8888_to_8888(image& target, int32_t x, int32_t y, const image& source);

static inline void image_insert_noov(image& target, int32_t x, int32_t y, const image& source)
{
	//dst\src  x  0  a   ba
	//  x      =  =  C0  ?=
	//  0      0  =  C0  ?=0
	//  a      1  1  Ca  ?=
	//  ba     1  1  C0  ?=
	// = - bitwise copy
	// 0 - bitwise copy, but set alpha to 0
	// 1 - bitwise copy, but set alpha to 1
	// ?= - bitwise copy if opaque, otherwise leave unchanged
	// ?=0 - if opaque, bitwise copy but set alpha to 0; otherwise leave unchanged
	// C0 - composite, but set output alpha to zero
	// Ca - composite, but assume A=1 or A=0 are the common cases
	// dst=ba src=a with non-ba-compliant src will yield shitty results,
	//  but there is no possible answer so the only real solution is banning it in the API.
	
	if (source.fmt == ifmt_bargb8888 && target.fmt == ifmt_0rgb8888)
		return image_insert_noov_8888_to_8888<false, true, 0x00000000>(target, x, y, source);
	if (source.fmt == ifmt_bargb8888)
		return image_insert_noov_8888_to_8888<false, true, -1>(target, x, y, source);
	if (source.fmt == ifmt_argb8888 && target.fmt == ifmt_0rgb8888)
		return image_insert_noov_8888_to_8888<true, true, 0x00000000>(target, x, y, source);
	if (source.fmt == ifmt_argb8888)
		return image_insert_noov_8888_to_8888<true, true, -1>(target, x, y, source);
	if (target.fmt == ifmt_argb8888 || target.fmt == ifmt_bargb8888)
		return image_insert_noov_8888_to_8888<false, false, 0xFF000000>(target, x, y, source);
	if (target.fmt == ifmt_0rgb8888 && source.fmt == ifmt_xrgb8888)
		return image_insert_noov_8888_to_8888<false, false, 0x00000000>(target, x, y, source);
	else
		return image_insert_noov_8888_to_8888<false, false, -1>(target, x, y, source);
}

template<bool composite, bool checkalpha, uint32_t newalpha>
static inline void image_insert_noov_8888_to_8888(image& target, int32_t x, int32_t y, const image& source)
{
	for (uint32_t yy=0;yy<source.height;yy++)
	{
		uint32_t* targetpx = (uint32_t*)target.pixels + (y+yy)*target.stride/sizeof(uint32_t) + x;
		uint32_t* sourcepx = (uint32_t*)source.pixels + yy*source.stride/sizeof(uint32_t);
		
		for (uint32_t xx=0;xx<source.width;xx++)
		{
			uint8_t alpha = sourcepx[xx] >> 24;
			if (checkalpha && UNLIKELY(alpha != 255) && UNLIKELY(composite || alpha == 0))
			{
				if (composite && UNLIKELY(alpha != 0))
				{
					uint8_t sr = sourcepx[xx]>>0;
					uint8_t sg = sourcepx[xx]>>8;
					uint8_t sb = sourcepx[xx]>>16;
					uint8_t sa = sourcepx[xx]>>24;
					
					uint8_t tr = targetpx[xx]>>0;
					uint8_t tg = targetpx[xx]>>8;
					uint8_t tb = targetpx[xx]>>16;
					uint8_t ta = targetpx[xx]>>24;
					
					tr = (sr*sa/255) + (tr*(255-sa)/255);
					tg = (sg*sa/255) + (tg*(255-sa)/255);
					tb = (sb*sa/255) + (tb*(255-sa)/255);
					if (newalpha == (uint32_t)-1)
						ta = (sa*sa/255) + (ta*(255-sa)/255);
					else
						ta = newalpha>>24;
					
					targetpx[xx] = ta<<24 | tb<<16 | tg<<8 | tr<<0;
				}
				//else a=0 -> do nothing
			}
			else
			{
				if (newalpha != (uint32_t)-1)
					targetpx[xx] = newalpha | (sourcepx[xx]&0x00FFFFFF);
				else
					targetpx[xx] = sourcepx[xx];
			}
		}
	}
}



//this one requires height <= other.height
static void image_insert_tile_row(image& target, int32_t x, int32_t y, uint32_t width, uint32_t height, const image& other)
{
	uint32_t xx = 0;
	if (width >= other.width)
	{
		for (xx = 0; xx < width-other.width; xx += other.width)
		{
			target.insert_sub(x+xx, y, other, 0, 0, other.width, height);
		}
	}
	if (xx < width)
	{
		target.insert_sub(x+xx, y, other, 0, 0, width-xx, height);
	}
}
void image::insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height, const image& other)
{
	uint32_t yy = 0;
	if (height >= other.height)
	{
		for (yy = 0; yy < height-other.height; yy += other.height)
		{
			image_insert_tile_row(*this, x, y+yy, width, other.height, other);
		}
	}
	
	if (yy < height)
	{
		image_insert_tile_row(*this, x, y+yy, width, height-yy, other);
	}
}


static void image_insert_tile_row(image& target, int32_t x, int32_t y, uint32_t width, uint32_t height,
                                  const image& other, uint32_t offx, uint32_t offy)
{
	if (offx + width <= other.width)
	{
		target.insert_sub(x, y, other, offx, offy, width, height);
		return;
	}
	
	if (offx != 0)
	{
		target.insert_sub(x, y, other, offx, offy, other.width-offx, height);
		x += other.width-offx;
		width -= other.width-offx;
	}
	uint32_t xx = 0;
	if (width >= other.width)
	{
		for (xx = 0; xx < width-other.width; xx += other.width)
		{
			target.insert_sub(x+xx, y, other, 0, offy, other.width, height);
		}
	}
	if (xx < width)
	{
		target.insert_sub(x+xx, y, other, 0, offy, width-xx, height);
	}
}
void image::insert_tile(int32_t x, int32_t y, uint32_t width, uint32_t height,
                        const image& other, uint32_t offx, uint32_t offy)
{
	if (offy + height <= other.height)
	{
		image_insert_tile_row(*this, x, y, width, height, other, offx, offy);
		return;
	}
	
	if (offy != 0)
	{
		image_insert_tile_row(*this, x, y, width, other.height-offy, other, offx, offy);
		y += other.height-offy;
		height -= other.height-offy;
	}
	
	uint32_t yy = 0;
	if (height >= other.height)
	{
		for (yy = 0; yy < height-other.height; yy += other.height)
		{
			image_insert_tile_row(*this, x, y+yy, width, other.height, other, offx, 0);
		}
	}
	
	if (yy < height)
	{
		image_insert_tile_row(*this, x, y+yy, width, height-yy, other, offx, 0);
	}
}



void image::insert_tile_with_border(int32_t x, int32_t y, uint32_t width, uint32_t height,
                                    const image& other, uint32_t x1, uint32_t x2, uint32_t y1, uint32_t y2)
{
	uint32_t x3 = other.width;
	uint32_t y3 = other.height;
	
	uint32_t w1 = x1-0;
	uint32_t w2 = x2-x1;
	uint32_t w3 = x3-x2;
	uint32_t h1 = y1-0;
	uint32_t h2 = y2-y1;
	uint32_t h3 = y3-y2;
	
	image sub;
	
	insert_sub(x,          y, other, 0,  0, x1, y1); // top left
	sub.init_ref_sub(other, x1, 0, w2, h1); insert_tile(x+x1,        y,           width-w1-w3, h1,           sub); // top
	insert_sub(x+width-w3, y, other, x2, 0, w3, y1); // top right
	
	sub.init_ref_sub(other, 0,  y1, w1, h2); insert_tile(x,          y+y1,        w1,          height-h1-h3, sub); // left
	sub.init_ref_sub(other, x1, y1, w2, h2); insert_tile(x+x1,       y+y1,        width-w1-w3, height-h1-h3, sub); // middle
	sub.init_ref_sub(other, x2, y1, w3, h2); insert_tile(x+width-w3, y+y1,        w3,          height-h1-h3, sub); // right
	
	insert_sub(x,          y+height-h3, other, 0,  y2, x1, h3); // bottom left
	sub.init_ref_sub(other, x1, y2, w2, h3); insert_tile(x+x1,       y+height-h3, width-w1-w3, h3,           sub); // bottom
	insert_sub(x+width-w3, y+height-h3, other, x2, y2, w3, h3); // bottom right
}



void image::init_clone(const image& other, int32_t scalex, int32_t scaley)
{
	bool flipx = (scalex<0);
	bool flipy = (scaley<0);
	scalex = abs(scalex);
	scaley = abs(scaley);
	
	init_new(other.width * scalex, other.height * scaley, other.fmt);
	for (uint32_t y=0;y<other.height;y++)
	{
		int ty = y*scaley;
		if (flipy) ty = (height-1-y)*scaley;
		uint32_t* targetpx = (uint32_t*)this->pixels + ty*this->stride/sizeof(uint32_t);
		uint32_t* sourcepx = (uint32_t*)other.pixels +  y*other.stride/sizeof(uint32_t);
		uint32_t* targetpxorg = targetpx;
		
		if (flipx)
		{
			sourcepx += width;
			for (uint32_t x=0;x<other.width;x++)
			{
				sourcepx--;
				for (int32_t xx=0;xx<scalex;xx++)
				{
					*(targetpx++) = *sourcepx;
				}
			}
		}
		else
		{
			for (uint32_t x=0;x<other.width;x++)
			{
				for (int32_t xx=0;xx<scalex;xx++)
				{
					*(targetpx++) = *sourcepx;
				}
				sourcepx++;
			}
		}
		
		for (int32_t yy=1;yy<scaley;yy++)
		{
			memcpy(targetpxorg+yy*stride/sizeof(uint32_t), targetpxorg, sizeof(uint32_t)*width);
		}
	}
}



bool image::init_decode(arrayview<byte> data)
{
	this->fmt = ifmt_none;
	return
		init_decode_png(data) ||
		false;
}



#include "test.h"

test("image byte per pixel", "", "imagebase")
{
	assert_eq(image::byteperpix(ifmt_rgb565), 2);
	assert_eq(image::byteperpix(ifmt_rgb888), 3);
	assert_eq(image::byteperpix(ifmt_xrgb1555), 2);
	assert_eq(image::byteperpix(ifmt_xrgb8888), 4);
	assert_eq(image::byteperpix(ifmt_0rgb1555), 2);
	assert_eq(image::byteperpix(ifmt_0rgb8888), 4);
	assert_eq(image::byteperpix(ifmt_argb1555), 2);
	assert_eq(image::byteperpix(ifmt_argb8888), 4);
	assert_eq(image::byteperpix(ifmt_bargb1555), 2);
	assert_eq(image::byteperpix(ifmt_bargb8888), 4);
}
