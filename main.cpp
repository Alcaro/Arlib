#include "arlib.h"

widget_listbox_virtual* vlist;
const char * c(size_t row, int col)
{
	static int i;
	i++;
	static char ret[16];
	sprintf(ret, "%i", i);
	return ret;
}

void q(size_t row)
{
	vlist->refresh();
}

#include<windows.h>
int main(int argc, char * argv[])
{
	WUTfEnableArgs(&argc, &argv);
	// this makes Wine spam 'pf_printf_a multibyte characters printing not supported', lol
	for (int i=0;argv[1] && argv[1][i];i++) printf("(%c)",argv[1][i]);
	HANDLE e = CreateFileA("sm\xC3\xB6rg\xC3\xA5sr\xC3\xA4ka.txt", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
	//HANDLE e = CreateFileW(L"smörgåsräka.txt", GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	puts("a");
	if (e==INVALID_HANDLE_VALUE){ puts("NOOOOOOOOOO"); return 0; }
	puts("b");
	char p[42];
	memset(p,0,42);
	DWORD i;
	ReadFile(e,p,42,&i,NULL);
	puts("c");
	puts(p);
	puts("d");

	//puts("a");
	//FILE* e=fopen("sm\xC3\xB6rg\xC3\xA5sr\xC3\xA4ka.txt", "rt");
	//if (!e) { puts("NOOOOOOOOOO"); return 0; }
	//puts("b");
	//char p[42];
	//memset(p,0,42);
	//fread(p,1,42,e);
	//puts("c");
	//puts(p);
	//puts("d");
	
	return 0;
	
	window_init(&argc, &argv);
	
	widget_listbox_virtual* list = widget_create_listbox_virtual("AAA", "BBB", "CCC");
	list->set_contents(bind(c), NULL);
	list->set_num_rows(10000000);
	const char * g[]={"COWW", "COW", "COW"};
	list->set_size(10, g, -1);
	list->add_checkboxes(NULL);
	
	//widget_listbox* list = widget_create_listbox("COW", "COWW", "COW");
	//for (int i=0;i<200;i++)
	//{
	//	list->add_row("COW", "COW", "COWW");
	//}
	//const char * g[]={"COW", "COW", "COWW"};
	//list->set_size(10, g, -1);
	list->set_on_focus_change(bind(q));
	
	vlist = list;
	
	window* w = window_create(list);
	w->set_resizable(true, NULL);
	
	w->set_visible(true);
	while (w->is_visible()) window_run_wait();
	
	delete w;
}
