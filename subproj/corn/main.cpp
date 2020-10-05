#include "arlib.h"
#include <gtk/gtk.h>
#include <gst/gst.h>
#include <sys/stat.h>

#define MUSIC_DIR "/home/alcaro/Desktop/extramusic/"

namespace {

#define NUM_ROWS 58 // 995px = 58*17+9
#define NUM_COLS 4

struct textgrid {
	string text[NUM_ROWS*NUM_COLS];
	int focus_row[NUM_COLS];
	function<void(int col, int row)> onactivate[NUM_COLS];
	
	GtkWidget* widget;
	PangoLayout* lay = NULL;
	
	void init()
	{
		widget = gtk_drawing_area_new();
		
		auto draw_cb = decompose_lambda([this](cairo_t* cr, GtkWidget* widget)->gboolean
		{
			GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
			if (!this->lay) this->lay = gtk_widget_create_pango_layout(widget, "");
			
			int full_width = gtk_widget_get_allocated_width(widget);
			int full_height = gtk_widget_get_allocated_height(widget);
			
			int idx = 0;
			for (int col=0;col<NUM_COLS;col++)
			for (int row=0;row<NUM_ROWS;row++)
			{
				if (this->text[idx])
				{
					double loc_x1 =  col    * full_width  / (double)NUM_COLS;
					double loc_x2 = (col+1) * full_width  / (double)NUM_COLS;
					double loc_y1 =  row    * full_height / (double)NUM_ROWS;
					double loc_y2 = (row+1) * full_height / (double)NUM_ROWS;
					
					cairo_reset_clip(cr);
					cairo_rectangle(cr, loc_x1, loc_y1, loc_x2-loc_x1, loc_y2-loc_y1);
					cairo_clip(cr);
					
					if (this->focus_row[col] == row)
					{
						gtk_style_context_set_state(ctx, GTK_STATE_FLAG_SELECTED);
						gtk_render_background(ctx, cr, loc_x1, loc_y1, loc_x2-loc_x1, loc_y2-loc_y1);
					}
					
					pango_layout_set_text(lay, this->text[idx], -1);
					double baseline_in_pos = pango_layout_get_baseline(lay); // where it is
					double baseline_out_pos = (double)14/17 * (loc_y2-loc_y1); // where I want it
					gtk_render_layout(ctx, cr, loc_x1, loc_y1 + baseline_out_pos - baseline_in_pos/PANGO_SCALE, lay);
					
					if (this->focus_row[col] == row)
					{
						gtk_style_context_set_state(ctx, GTK_STATE_FLAG_NORMAL);
					}
				}
				idx++;
			}
			
			return false;
		});
		g_signal_connect_swapped(widget, "draw", G_CALLBACK(draw_cb.fp), draw_cb.ctx);
		
		auto click_cb = decompose_lambda([this](GdkEvent* event, GtkWidget* widget)->gboolean
		{
			if(0);
			else if (event->button.button == GDK_BUTTON_PRIMARY && event->type == GDK_2BUTTON_PRESS) {}
			else if (event->button.button == GDK_BUTTON_MIDDLE && event->type == GDK_BUTTON_PRESS) {}
			else return GDK_EVENT_STOP;
			
			int col = event->button.x * NUM_COLS / gtk_widget_get_allocated_width(widget);
			int row = event->button.y * NUM_ROWS / gtk_widget_get_allocated_height(widget);
			this->onactivate[col](col, row);
			
			return GDK_EVENT_STOP;
		});
		g_signal_connect_swapped(widget, "button-press-event", G_CALLBACK(click_cb.fp), click_cb.ctx);
		gtk_widget_add_events(widget, GDK_BUTTON_PRESS_MASK);
		
		for (int& foc : focus_row)
			foc = -1;
	}
};
textgrid grid;



GtkWindow* mainwnd = NULL;
GQuark q_fname;
GstElement* pipeline = NULL;
void play(cstring fn);
void enqueue(string fn);

GtkButton* btn_toggle;
GtkWidget* btnimg_stop;
GtkWidget* btnimg_play;

template<int t_start, int t_width> struct column {
	static const int start = t_start;
	static const int width = t_width;
	
	static const size_t offset = NUM_ROWS*start;
	static const size_t n_cells = NUM_ROWS*width;
	
	size_t n_items = 0;
	string items[n_cells];
	
	function<void(cstring)> fn;
	
	void init(function<void(cstring)> fn)
	{
		this->fn = fn;
		for (size_t n=0;n<width;n++)
		{
			grid.onactivate[start+n] = [this](int col, int row) {
				this->fn(this->items[(col-start)*NUM_ROWS + row]);
			};
		}
	}
	
	bool add_raw(cstring display, cstring full)
	{
		if (n_items == n_cells) return false;
		
		items[n_items] = full;
		grid.text[offset+n_items] = display;
		n_items++;
		gtk_widget_queue_draw(grid.widget);
		return true;
	}
	
	bool add(cstring fn)
	{
		return add_raw(file::basename(fn), fn);
	}
	
	void remove_first()
	{
		for (size_t i=0;i<n_items-1;i++)
		{
			items[i] = std::move(items[i+1]);
			grid.text[offset+i] = std::move(grid.text[offset+i+1]);
		}
		
		items[n_items-1] = "";
		grid.text[offset+n_items-1] = "";
		n_items--;
		gtk_widget_queue_draw(grid.widget);
	}
	
	void reset()
	{
		n_items = 0;
		for (size_t i=0;i<n_cells;i++)
		{
			items[i] = "";
			grid.text[i+offset] = "";
		}
		focus(-1);
		gtk_widget_queue_draw(grid.widget);
	}
	
	void focus(int idx)
	{
		for (size_t n=0;n<width;n++)
		{
			grid.focus_row[start+n] = -1;
		}
		if (idx != -1)
			grid.focus_row[start + idx/NUM_ROWS] = idx%NUM_ROWS;
		gtk_widget_queue_draw(grid.widget);
	}
};
column<0,2> c_common;
column<2,1> c_search;
column<3,1> c_playlist;


GtkProgressBar* progress;
GtkWidget* make_progress()
{
	progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
	gtk_progress_bar_set_show_text(progress, true);
	gtk_progress_bar_set_text(progress, "");
	gtk_progress_bar_set_fraction(progress, 0);
	return GTK_WIDGET(progress);
}

void init_common()
{
	c_common.reset();
	for (string line : file::readallt(file::exepath()+"common.txt").split("\n"))
	{
		if (line)
			c_common.add(MUSIC_DIR+line);
	}
}


GtkEntry* search_line;
size_t search_focus = 0;

array<string> search_filenames;
void init_search_recurse(cstring path)
{
	DIR* dir = opendir(MUSIC_DIR + path);
	dirent* ent;
	while ((ent = readdir(dir)))
	{
		// don't use strcmp, its constant overhead is suboptimal at best
		if (UNLIKELY(ent->d_name[0] == '.'))
		{
			if (ent->d_name[1] == '\0') continue;
			if (ent->d_name[1] == '.' && ent->d_name[2] == '\0') continue;
		}
		
		string childpath = path + ent->d_name;
		bool is_dir;
		if (ent->d_type == DT_UNKNOWN)
		{
			struct stat st;
			stat(MUSIC_DIR + childpath, &st);
			is_dir = (S_ISDIR(st.st_mode));
		}
		else if (ent->d_type == DT_DIR) is_dir = true;
		else is_dir = false;
		
		if (is_dir) init_search_recurse(childpath+"/");
		else search_filenames.append(childpath);
	}
	closedir(dir);
}
void init_search()
{
	search_filenames.reset();
	init_search_recurse("");
}
void update_search()
{
	//rules:
	// each potential match must contain every word in the filename (including directory, but not MUSIC_DIR), in order, case insensitive
	// additionally, the last word in the search string must be in the file's basename
	// a word is defined as space-separated sequence of nonspaces in search string
	// a match's penalty is defined as how many skip slots contain ascii non-space bytes
	// a skip slot is defined as each space, plus start of the string; start gives 1.5 penalty points
	// if the filename contains a slash, add 1 penalty point
	//output:
	// all matches, sorted by ascending penalty, then alphabetically by name including dir
	// if there are matches than what fit, delete every match sharing the max penalty; repeat if needed
	// if the above did anything, or if there were no matches, insert an entry saying so
	//exception: if input is blank, output is blank
	//(implementation doubles the penalty)
	
	string key = gtk_entry_get_text(search_line);
	
	c_search.reset();
	search_focus = 0;
	if (!key) return;
	
	struct match_t {
		cstring fn;
		int penalty;
	};
	array<match_t> matches;
	array<cstring> words = key.csplit(" ");
	
	for (cstring fn : search_filenames)
	{
		int penalty = 0;
		
		size_t last_slash = fn.lastindexof("/");
		if (last_slash != (size_t)-1)
			penalty += 2;
		
		size_t search_at = 0;
		for (size_t word_at : range(words.size()))
		{
			size_t min_idx = search_at;
			if (word_at == words.size()-1 && last_slash != (size_t)-1)
				min_idx = max(min_idx, last_slash);
			size_t next_start = fn.iindexof(words[word_at], min_idx);
			if (next_start == (size_t)-1) goto not_match;
			
			size_t skipped = search_at;
			if (next_start>0 && fn[next_start-1]!='/')
			{
				while (skipped < next_start)
				{
					if (isalnum(fn[skipped]))
					{
						if (word_at == 0) penalty += 3;
						else penalty += 2;
						break;
					}
					skipped++;
				}
			}
			
			search_at = next_start + words[word_at].length();
		}
		
		matches.append({ fn, penalty });
	not_match: ;
	}
	
	matches.sort([](match_t& a, match_t& b){
		if (a.penalty != b.penalty) return a.penalty < b.penalty;
		return string::less(a.fn, b.fn);
	});
	
	c_search.reset();
	size_t n_matches_full = matches.size();
	if (matches.size() > c_search.n_cells)
	{
		int max_penalty = matches[c_search.n_cells-1].penalty;
		size_t trunc_to = 0;
		while (matches[trunc_to].penalty < max_penalty) trunc_to++;
		matches.resize(trunc_to);
	}
	if (!matches)
	{
		c_search.add_raw("("+tostring(n_matches_full)+" matches)", "");
		return;
	}
	
	for (match_t& m : matches)
		c_search.add(MUSIC_DIR+m.fn);
	c_search.focus(0);
	if (n_matches_full != matches.size())
	{
		c_search.add_raw("("+tostring(n_matches_full-matches.size())+" more matches)", "");
	}
}

GtkWidget* make_search()
{
	init_search();
	
	search_line = GTK_ENTRY(gtk_entry_new());
	auto key_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget)->gboolean
	{
		if (event->key.keyval == GDK_KEY_Up)
		{
			if (search_focus == 0)
				search_focus = c_search.n_items-1;
			else
				search_focus--;
			c_search.focus(search_focus);
			return GDK_EVENT_STOP;
		}
		if (event->key.keyval == GDK_KEY_Down)
		{
			if (search_focus == c_search.n_items-1)
				search_focus = 0;
			else
				search_focus++;
			c_search.focus(search_focus);
			return GDK_EVENT_STOP;
		}
		if (event->key.keyval == GDK_KEY_Return || event->key.keyval == GDK_KEY_KP_Enter)
		{
			if (!strcmp(gtk_entry_get_text(search_line), "."))
			{
				init_search();
				init_common();
			}
			else
				enqueue(c_search.items[search_focus]);
			if ((event->key.state&GDK_SHIFT_MASK) == 0)
				gtk_entry_set_text(search_line, ""); // calls remove_cb
			return GDK_EVENT_STOP;
		}
		if (event->key.keyval == GDK_KEY_Escape)
		{
			gtk_entry_set_text(search_line, "");
			return GDK_EVENT_STOP;
		}
		return GDK_EVENT_PROPAGATE;
	});
	g_signal_connect_swapped(search_line, "key-press-event", G_CALLBACK(key_cb.fp), key_cb.ctx);
	// GTK often passes functions with too few arguments if the latter ones aren't used (for example gtk_widget_hide_on_delete)
	// it works in practice, but I'm 99% sure it's undefined behavior, so I'm not gonna follow suit.
	auto insert_cb = decompose_lambda([](unsigned position, char* chars, unsigned n_chars, GtkEntryBuffer* buffer)
	{
		update_search();
	});
	g_signal_connect_swapped(gtk_entry_get_buffer(search_line), "inserted-text", G_CALLBACK(insert_cb.fp), insert_cb.ctx);
	auto remove_cb = decompose_lambda([](unsigned position, unsigned n_chars, GtkEntryBuffer* buffer)
	{
		update_search();
	});
	g_signal_connect_swapped(gtk_entry_get_buffer(search_line), "deleted-text", G_CALLBACK(remove_cb.fp), remove_cb.ctx);
	
	return GTK_WIDGET(search_line);
}


size_t playlist_cur_idx = 0;
void playlist_run()
{
	c_playlist.focus(-1);
	if (playlist_cur_idx == c_playlist.n_items) return;
	cstring next = c_playlist.items[playlist_cur_idx];
	if (!next) return;
	c_playlist.focus(playlist_cur_idx);
	play(next);
}
void enqueue(string fn) // cstring explodes if it's the oldest playlist item, so use string
{
	if (!fn) return;
	if (!c_playlist.add(fn))
	{
		if (playlist_cur_idx == 0)
			return;
		
		playlist_cur_idx--;
		c_playlist.remove_first();
		c_playlist.focus(playlist_cur_idx);
		
		c_playlist.add(fn);
	}
	if (!pipeline) playlist_run();
}


runloop::g_timer progress_timer;
guint bus_watch = 0;

void stop()
{
	if (!pipeline) return;
	gst_element_set_state(pipeline, GST_STATE_NULL);
	gst_object_unref(pipeline);
	pipeline = NULL;
	
	progress_timer.reset();
	gtk_progress_bar_set_text(progress, "");
	gtk_progress_bar_set_fraction(progress, 0);
	
	gtk_button_set_image(btn_toggle, btnimg_play);
	if (bus_watch != 0) g_source_remove(bus_watch);
}

void progress_tick()
{
	if (!pipeline || !progress) return;
	gtk_button_set_image(btn_toggle, btnimg_stop);
	
	int64_t song_at;
	gst_element_query_position(pipeline, GST_FORMAT_TIME, &song_at);
	int64_t song_len;
	gst_element_query_duration(pipeline, GST_FORMAT_TIME, &song_len);
	
	uint32_t sec_at = (song_at+500000000)/1000000000;
	uint32_t sec_len = (song_len+500000000)/1000000000;
	char buf[32];
	if (GST_CLOCK_TIME_IS_VALID(song_len))
		sprintf(buf, "%u:%.2u / %u:%.2u", sec_at/60, sec_at%60, sec_len/60, sec_len%60);
	else
		sprintf(buf, "%u:%.2u / ?:??", sec_at/60, sec_at%60);
	gtk_progress_bar_set_text(progress, buf);
	if (GST_CLOCK_TIME_IS_VALID(song_len))
		gtk_progress_bar_set_fraction(progress, (double)sec_at/sec_len);
}

void play(cstring fn)
{
	stop();
	
	pipeline = gst_parse_launch("filesrc location=\""+fn+"\" ! decodebin ! autoaudiosink", NULL);
	gst_element_set_state(pipeline, GST_STATE_PLAYING);
	
	GstBus* bus = gst_element_get_bus(pipeline);
	gst_bus_add_watch(bus, [](GstBus* bus, GstMessage* message, void* user_data)->gboolean {
			progress_tick();
			if (message->type == GST_MESSAGE_EOS)
			{
				bus_watch = 0;
				stop();
				playlist_cur_idx++;
				playlist_run();
				return G_SOURCE_REMOVE;
			}
			return G_SOURCE_CONTINUE;
		}, NULL);
	gst_object_unref(bus);
	
	progress_timer.set_repeat(1000, progress_tick);
}


GtkWidget* make_button(const char * name, void(*cb)())
{
	GtkButton* btn = GTK_BUTTON(gtk_button_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON));
	auto click_cb = decompose_lambda([cb](GtkButton* btn) { cb(); });
	g_signal_connect_swapped(btn, "clicked", G_CALLBACK(click_cb.fp), click_cb.ctx);
	return GTK_WIDGET(btn);
}

void playlist_prev()
{
	if (!playlist_cur_idx) return;
	playlist_cur_idx--;
	stop();
	playlist_run();
}
void playlist_next()
{
	if (playlist_cur_idx >= c_playlist.n_items-1) return;
	playlist_cur_idx++;
	stop();
	playlist_run();
}
void playlist_toggle()
{
	if (pipeline) stop();
	else playlist_run();
}

void make_gui(GApplication* application)
{
	if (mainwnd) return;
	q_fname = g_quark_from_static_string("corn-filename");
	
	if (application) mainwnd = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
	else
	{
		mainwnd = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
		auto onclose_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget) -> gboolean
		{
			gtk_main_quit();
			return GDK_EVENT_STOP;
		});
		g_signal_connect_swapped(mainwnd, "delete-event", G_CALLBACK(onclose_cb.fp), onclose_cb.ctx);
	}
	
	auto key_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget)->gboolean
	{
		if (!gtk_widget_is_focus(GTK_WIDGET(search_line)))
			gtk_widget_grab_focus(GTK_WIDGET(search_line));
		return FALSE;
	});
	g_signal_connect_swapped(mainwnd, "key-press-event", G_CALLBACK(key_cb.fp), key_cb.ctx);
	
	GtkBox* box_upper = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	gtk_box_pack_start(box_upper, make_search(), true, true, 0);
	gtk_box_pack_start(box_upper, make_button("gtk-media-previous", playlist_prev), false, false, 0);
	btnimg_play = gtk_image_new_from_icon_name("gtk-media-play", GTK_ICON_SIZE_BUTTON);
	btnimg_stop = gtk_image_new_from_icon_name("gtk-media-stop", GTK_ICON_SIZE_BUTTON);
	g_object_ref(btnimg_play);
	g_object_ref(btnimg_stop);
	btn_toggle = GTK_BUTTON(make_button("gtk-media-play", playlist_toggle));
	gtk_box_pack_start(box_upper, GTK_WIDGET(btn_toggle), false, false, 0);
	gtk_box_pack_start(box_upper, make_progress(), false, false, 0);
	gtk_box_pack_start(box_upper, make_button("gtk-media-next", playlist_next), false, false, 0);
	
	c_common.init(enqueue);
	c_search.init(enqueue);
	c_playlist.init(enqueue);
	grid.init();
	
	GtkBox* box_main = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
	gtk_box_pack_start(box_main, GTK_WIDGET(box_upper), false, false, 0);
	gtk_box_pack_start(box_main, GTK_WIDGET(grid.widget), true, true, 0);
	
	gtk_container_add(GTK_CONTAINER(mainwnd), GTK_WIDGET(box_main));
	
	init_common();
	
	gtk_window_set_resizable(mainwnd, true);
	
	gtk_window_resize(mainwnd, 1336, 1026);
	gtk_widget_show_all(GTK_WIDGET(mainwnd));
	gdk_window_move(gtk_widget_get_window(GTK_WIDGET(mainwnd)), 582, 0);
}


void app_open(GApplication* application, GFile* * files, int n_files, char* hint, void* user_data)
{
	make_gui(application);
	for (int i : range(n_files)) enqueue(g_file_peek_path(files[i]));
}

void app_activate(GApplication* application, void* user_data)
{
	make_gui(application);
}

}

#include <sys/resource.h>
int main(int argc, char** argv)
{
	gst_init(&argc, &argv);
	arlib_init_manual_args(&argc, &argv);
	
	/*
	(corn:2350): GLib-GObject-CRITICAL **: 14:58:36.794: g_object_ref: assertion 'G_IS_OBJECT (object)' failed
	
	Thread 329 "gldisplay-event" received signal SIGTRAP, Trace/breakpoint trap.
	[Switching to Thread 0x7fffb2df8700 (LWP 5929)]
	0x00007ffff7580ea1 in ?? () from /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	(gdb) bt
	#0  0x00007ffff7580ea1 in  () at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#1  0x00007ffff75821dd in g_logv ()
		at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#2  0x00007ffff758231f in g_log ()
		at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#3  0x00007ffff785ac3c in g_object_ref ()
		at /usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0
	#4  0x00007ffff7aca5e7 in gst_object_ref ()
		at /usr/lib/x86_64-linux-gnu/libgstreamer-1.0.so.0
	#5  0x00007fffc0cc78a1 in  () at /usr/lib/x86_64-linux-gnu/libgstgl-1.0.so.0
	#6  0x00007fffc0cc7ccc in  () at /usr/lib/x86_64-linux-gnu/libgstgl-1.0.so.0
	#7  0x00007fffc0cc8fcc in  () at /usr/lib/x86_64-linux-gnu/libgstgl-1.0.so.0
	#8  0x00007ffff757b417 in g_main_context_dispatch ()
		at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#9  0x00007ffff757b650 in  () at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#10 0x00007ffff757b962 in g_main_loop_run ()
		at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#11 0x00007fffc0ca07f7 in  () at /usr/lib/x86_64-linux-gnu/libgstgl-1.0.so.0
	#12 0x00007ffff75a3175 in  () at /usr/lib/x86_64-linux-gnu/libglib-2.0.so.0
	#13 0x00007ffff4ac46db in start_thread (arg=0x7fffb2df8700)
		at pthread_create.c:463
	#14 0x00007ffff5209a3f in clone ()
		at ../sysdeps/unix/sysv/linux/x86_64/clone.S:95
	(gdb) c
	Continuing.
	
	(corn:2350): GStreamer-CRITICAL **: 15:01:41.883: gst_object_unref: assertion '((GObject *) object)->ref_count > 0' failed
	*/
	// why does it even use libgstgl when there's no video output
	g_log_set_always_fatal((GLogLevelFlags)0);
	
	if ((cstring)argv[1] == "--local")
	{
		make_gui(NULL);
		char** arg = argv+2;
		while (*arg) enqueue(*arg++);
		gtk_main();
		return 0;
	}
	
	GtkApplication* app = gtk_application_new("se.muncher.corn", G_APPLICATION_HANDLES_OPEN);
	g_signal_connect(app, "activate", G_CALLBACK(app_activate), NULL);
	g_signal_connect(app, "open", G_CALLBACK(app_open), NULL);
	
	return g_application_run(G_APPLICATION(app), argc, argv);
}
