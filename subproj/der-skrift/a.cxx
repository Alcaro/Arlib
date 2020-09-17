#!/usr/bin/env der-skrift

int twice(int a) { return a*2; }

int x = 1;

while (x < 1000)
{
	x = twice(x);
	printf("%d\n", x);
}
