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

int main(int argc, char * argv[])
{
	WUTfEnable();
	puts("a");
	FILE* e=fopen("sm\xC3\xB6rg\xC3\xA5sr\xC3\xA4ka.txt", "rt");
	if (!e) { puts("NOOOOOOOOOO"); return 0; }
	puts("b");
	char p[42];
	memset(p,0,42);
	fread(p,1,42,e);
	puts("c");
	puts(p);
	puts("d");
	
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
