#include "arlib.h"
#include <X11/Xutil.h>

static oimage get_screenshot(Display* display)
{
	Window root = DefaultRootWindow(display);
	
	XWindowAttributes wa;
	XGetWindowAttributes(display, root, &wa);
	
	XImage* x_img = XGetImage(display, root, 0,0, wa.width,wa.height, AllPlanes, ZPixmap);
	
	oimage ret = oimage::create(wa.width, wa.height, false, false);
	
	for (int y=0;y<wa.height;y++)
	{
		for (int x=0;x<wa.width;x++)
		{
			// optimizing this could reduce the function from 27ms to 18ms. Not worth the effort.
			ret[y][x] = XGetPixel(x_img, x, y) | 0xFF000000;
		}
	}
	
	XDestroyImage(x_img);
	return ret;
}

int main(int argc, char** argv)
{
	const char * displays[] = { ":0", ":1" };
	
	for (const char * name : displays)
	{
		Display* display = XOpenDisplay(name);
		if (!display)
			continue;
		
		oimage screenshot = get_screenshot(display);
		for (uint32_t y=0;y<screenshot.height;y++)
		for (uint32_t x=0;x<screenshot.width;x++)
		{
			if (screenshot[y][x] != 0xFF000000)
			{
				bytearray by = screenshot.encode_png();
				file2(argv[1], file2::m_replace).write(by);
				exit(0);
			}
		}
		XCloseDisplay(display);
	}
	puts("failed.");
}
