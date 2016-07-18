//#include "arlib.h"
#include "nall/string.hpp"
#include <stdio.h>
/*
rm callgrind.out.*; g++ -g -I. arlib/malloc.cpp arlib/bml.cpp main.cpp -oalcaro.exe -Os -std=c++11 && valgrind --tool=callgrind --dump-instr=yes --collect-jumps=yes ./alcaro.exe && kcachegrind callgrind.out.*
wine g++ -I. main.cpp -obyuu.exe -Os -std=c++14 && time wine byuu.exe
wine g++ -I. arlib/malloc.cpp arlib/bml.cpp main.cpp -oalcaro.exe -Os -std=c++14 && time wine alcaro.exe
*/
char cheatsbml[2224359+1];

int main(int argc, char * argv[])
{
	FILE* f = fopen("cheats.bml", "rt");
	cheatsbml[fread(cheatsbml, 1,2224359, f)]='\0';
	fclose(f);
	
	int score = 0;
	
	cheatsbml[0]=0;
	
	//bmlparser* parse = bmlparser::create(cheatsbml);
	//int depth = 0;
	//while (true)
	//{
		//bmlparser::event ev = parse->next();
		//if (ev.action == bmlparser::enter)
		//{
			//if (depth==0) score++;
			//depth++;
		//}
		//if (ev.action == bmlparser::exit) depth--;
		//if (ev.action == bmlparser::finish) break;
	//}
	
	auto doc = nall::BML::unserialize(cheatsbml);
	score = doc.size();
	
	if (score != 2001) puts("ERROR ERROR ERROR");
	printf("%i\n", score);
}
