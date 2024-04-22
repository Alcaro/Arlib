#include "arlib.h"
#include "play.h"
#include "obj/resources.h"
#include <gtk/gtk.h>
#ifdef ARLIB_GUI_GTK4
#include <gdk/x11/gdkx.h>
#endif
#include <sys/stat.h>

// this tool is designed for personal use only, hardcoded stuff is fine for all intended usecases
#define MUSIC_DIR "/home/walrus/Desktop/extramusic/"

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
		
#ifdef ARLIB_GUI_GTK3
		auto draw_cb = decompose_lambda([this](cairo_t* cr, GtkWidget* widget)->gboolean
#else
		auto draw_cb = decompose_lambda([](void* user_data){ return user_data; },
			[this](GtkDrawingArea* drawing_area, cairo_t* cr, int width, int height, void* user_data)
#endif
		{
			GtkStyleContext* ctx = gtk_widget_get_style_context(widget);
			if (!this->lay) this->lay = gtk_widget_create_pango_layout(widget, "");
			
#ifdef ARLIB_GUI_GTK3
			int width = gtk_widget_get_allocated_width(widget);
			int height = gtk_widget_get_allocated_height(widget);
#endif
			
			int idx = 0;
			for (int col=0;col<NUM_COLS;col++)
			for (int row=0;row<NUM_ROWS;row++)
			{
				if (this->text[idx])
				{
					// Gtk knows the colors, but I can't find how to use them. Just hardcode something.
					// It's less likely to be deprecated or otherwise break in Gtk5, anyways.
					
					// round to integer - subpixel rendering looks stupid
					int loc_x1 =  col    * width  / NUM_COLS;
					int loc_x2 = (col+1) * width  / NUM_COLS;
					int loc_y1 =  row    * height / NUM_ROWS;
					int loc_y2 = (row+1) * height / NUM_ROWS;
					
					cairo_reset_clip(cr);
					cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
					cairo_rectangle(cr, loc_x1, loc_y1, loc_x2-loc_x1, loc_y2-loc_y1);
					cairo_clip(cr);
					
					if (this->focus_row[col] == row)
					{
						cairo_set_source_rgb(cr, 25/255.0, 132/255.0, 228/255.0);
						cairo_rectangle(cr, loc_x1, loc_y1, loc_x2-loc_x1, loc_y2-loc_y1);
						cairo_fill(cr);
						cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
					}
					
					pango_layout_set_text(lay, this->text[idx], -1);
					double baseline_in_pos = pango_layout_get_baseline(lay); // where it is
					double baseline_out_pos = (double)14/17 * (loc_y2-loc_y1); // where I want it
					
					cairo_move_to(cr, loc_x1, loc_y1 + (int)(baseline_out_pos - baseline_in_pos/PANGO_SCALE));
					pango_cairo_show_layout(cr, lay);
				}
				idx++;
			}
#ifdef ARLIB_GUI_GTK3
			return false;
#endif
		});
#ifdef ARLIB_GUI_GTK3
		g_signal_connect_swapped(widget, "draw", G_CALLBACK(draw_cb.fp), draw_cb.ctx);
#else
		gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(widget), draw_cb.fp, draw_cb.ctx, NULL);
#endif
		
#ifdef ARLIB_GUI_GTK3
		auto click_cb = decompose_lambda([this](GdkEvent* event, GtkWidget* widget)->gboolean
		{
			if(0);
			else if (event->button.button == GDK_BUTTON_PRIMARY && (event->button.state&GDK_CONTROL_MASK))
			{
				if (event->type == GDK_BUTTON_PRESS) {}
				else return GDK_EVENT_STOP; // to discard the 2BUTTON_PRESS
			}
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
#else
		auto click_cb = decompose_lambda([this](int n_press, double x, double y, GtkGestureClick* self) {
			uint32_t button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(self));
			
			GdkEvent* ev = gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(self));
			uint32_t mask = (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_ALT_MASK);
			GdkModifierType mods = (GdkModifierType)(gdk_event_get_modifier_state(ev) & mask);
			
			if(0);
			else if (button == GDK_BUTTON_PRIMARY && (mods&GDK_CONTROL_MASK)) {}
			else if (button == GDK_BUTTON_PRIMARY && n_press == 2) {}
			else if (button == GDK_BUTTON_MIDDLE) {}
			else return;
			
			int col = x * NUM_COLS / gtk_widget_get_allocated_width(widget);
			int row = y * NUM_ROWS / gtk_widget_get_allocated_height(widget);
			this->onactivate[col](col, row);
		});
		GtkGesture* click_ctrl = gtk_gesture_click_new();
		gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click_ctrl), 0);
		gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click_ctrl), GTK_PHASE_CAPTURE);
		gtk_widget_add_controller(widget, GTK_EVENT_CONTROLLER(click_ctrl));
		g_signal_connect_swapped(click_ctrl, "pressed", G_CALLBACK(click_cb.fp), click_cb.ctx);
#endif
		
		gtk_widget_set_hexpand(widget, true);
		gtk_widget_set_vexpand(widget, true);
		
		for (int& foc : focus_row)
			foc = -1;
	}
};
textgrid grid;



GtkWindow* mainwnd = NULL;
GQuark q_fname;

void enqueue(cstring fn);
void enqueue_real(cstring fn);

GtkButton* btn_toggle;
#ifdef ARLIB_GUI_GTK3
GtkWidget* btnimg_stop;
GtkWidget* btnimg_play;
#endif

struct column {
	int col_start;
	int col_width;
	
	size_t offset;
	size_t capacity;
	
	//static const int start = t_start;
	//static const int width = t_width;
	
	//static const size_t offset = NUM_ROWS*start;
	//static const size_t n_cells = NUM_ROWS*width;
	
	struct node
	{
		string display;
		string filename;
		int at;
		int total;
	};
	array<node> items;
	
	function<void(cstring)> fn;
	
	void init(int start, int width, function<void(cstring)> fn)
	{
		this->col_start = start;
		this->col_width = width;
		
		offset = NUM_ROWS*start;
		capacity = NUM_ROWS*width;
		
		this->fn = fn;
		for (int n=0;n<width;n++)
		{
			grid.onactivate[start+n] = [this](int col, int row) {
				size_t idx = (col-col_start)*NUM_ROWS + row;
				if (idx < items.size())
					this->fn(this->items[idx].filename);
			};
		}
	}
	
	void render(int row)
	{
		node& n = items[row];
		if (n.total > 1)
			grid.text[offset+row] = "("+tostring(n.at)+"/"+tostring(n.total)+") "+n.display;
		else
			grid.text[offset+row] = n.display;
		gtk_widget_queue_draw(grid.widget);
	}
	
	bool add_raw(cstring display, cstring full)
	{
		if (items && full == items[items.size()-1].filename)
		{
			items[items.size()-1].total++;
			render(items.size()-1);
			return true;
		}
		
		if (items.size() == capacity) return false;
		
		grid.text[offset+items.size()] = display;
		node& n = items.append({ display, full, 1, 1 });
		gtk_widget_queue_draw(grid.widget);
		return true;
	}
	
	bool add(cstring fn)
	{
		if (fn.startswith("$")) return add_raw(fn, fn);
		else return add_raw(file::basename(fn), fn);
	}
	
	void remove_first()
	{
		items.remove(0);
		for (size_t i=0;i<items.size();i++)
			grid.text[offset+i] = std::move(grid.text[offset+i+1]);
		
		grid.text[offset+items.size()] = "";
		gtk_widget_queue_draw(grid.widget);
	}
	
	void reset()
	{
		items.reset();
		for (size_t i=0;i<capacity;i++)
			grid.text[i+offset] = "";
		focus(-1);
		gtk_widget_queue_draw(grid.widget);
	}
	
	void focus(int idx)
	{
		for (int n=0;n<col_width;n++)
		{
			grid.focus_row[col_start+n] = -1;
		}
		if (idx != -1)
			grid.focus_row[col_start + idx/NUM_ROWS] = idx%NUM_ROWS;
		gtk_widget_queue_draw(grid.widget);
	}
};
column c_common;
column c_search;
column c_playlist;


GtkProgressBar* progress;
GtkWidget* make_progress()
{
	progress = GTK_PROGRESS_BAR(gtk_progress_bar_new());
	gtk_progress_bar_set_show_text(progress, true);
	gtk_progress_bar_set_text(progress, "");
	gtk_progress_bar_set_fraction(progress, 0);
	gtk_widget_set_valign(GTK_WIDGET(progress), GTK_ALIGN_CENTER);
	return GTK_WIDGET(progress);
}

void init_common()
{
	c_common.reset();
	for (string line : file::readallt(MUSIC_DIR "common.txt").split("\n"))
	{
		if (line)
			c_common.add(MUSIC_DIR+line);
	}
}


GtkEntry* search_line;
size_t search_focus = 0;

array<string> search_filenames;
map<string,string> symlink_filenames;
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
		int type;
		if (ent->d_type == DT_UNKNOWN)
		{
			struct stat st;
			lstat(MUSIC_DIR + childpath, &st);
			type = (st.st_mode & S_IFMT);
		}
		else
		{
			static_assert(S_IFIFO == DT_FIFO<<12);
			static_assert(S_IFCHR == DT_CHR<<12);
			static_assert(S_IFDIR == DT_DIR<<12);
			static_assert(S_IFBLK == DT_BLK<<12);
			static_assert(S_IFREG == DT_REG<<12);
			static_assert(S_IFLNK == DT_LNK<<12);
			static_assert(S_IFSOCK == DT_SOCK<<12);
			// DT_WHT is 14, but S_IFWHT doesn't exist
			type = ent->d_type << 12;
		}
		
		if(0);
		else if (type == S_IFLNK)
		{
			search_filenames.append(childpath);
			symlink_filenames.insert(childpath, file::resolve(file::dirname(childpath), file::readlink(MUSIC_DIR + childpath)));
		}
		else if (type == S_IFDIR) init_search_recurse(childpath+"/");
		else if (type == S_IFREG) search_filenames.append(childpath);
		else puts("unknown filetype "+childpath);
	}
	closedir(dir);
}
void init_search()
{
	search_filenames.reset();
	symlink_filenames.reset();
	init_search_recurse("");
}
int penalty_for(arrayview<cstring> words, cstring fn)
{
	//rules:
	// each potential match must contain every word in the filename (including directory, but not MUSIC_DIR), in order, case insensitive
	// a word is defined as space-separated (potentially empty) sequence of nonspaces in search string
	// a match's penalty is defined as how many skip slots contain ascii non-space bytes
	// a skip slot is defined as each space, plus start of the string; start gives 1.5 penalty points
	// if the filename contains a slash, add 1 penalty point
	// if the filename contains /x/ or /x-, add 2.5 penalty points
	// if the last word in the search string is not in the file's basename, add 50 penalty points
	//output:
	// all matches, sorted by ascending penalty, then alphabetically by name including dir
	// if there are matches than what fit, delete every match sharing the max penalty; repeat if needed
	// if the above did anything, or if there were no matches, insert an entry saying so
	//exception: if input is blank, output is blank, not even the entry saying so
	//(implementation doubles the penalty, so it's all integers)
	
	int penalty = 0;
	
	if (fn.contains("/x/") || fn.contains("/x-"))
		penalty += 1000;
	if (fn.endswith(".png") || fn.endswith(".jpg"))
		penalty += 1000;
	if (fn.endswith(".txt") || fn.endswith(".pdf"))
		penalty += 1000;
	if (fn.endswith(".db") || fn.endswith(".ini"))
		penalty += 1000;
	if (fn.endswith(".zip") || fn.endswith(".rar"))
		penalty += 1000;
	if (fn.endswith(".m3u") || fn.endswith(".pls"))
		penalty += 1000;
	if (fn.endswith(".htm") || fn.endswith(".html") || fn.endswith(".url") || fn.endswith(".nfo"))
		penalty += 1000;
	if (fn.endswith(".mid"))
		penalty += 1000;
	
	size_t last_slash = fn.lastindexof("/");
	if (last_slash != (size_t)-1)
		penalty += 2;
	
	size_t search_at = 0;
	for (size_t word_at : range(words.size()))
	{
		size_t min_idx = search_at;
		if (word_at == words.size()-1 && last_slash != (size_t)-1)
		{
			if (fn.iindexof(words[word_at], last_slash) == (size_t)-1) penalty += 100;
		}
		size_t next_start = fn.iindexof(words[word_at], min_idx);
		if (next_start == (size_t)-1)
			return -1;
		
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
	
	return penalty;
}
void update_search()
{
	string prev_focus;
	if (c_search.items) prev_focus = c_search.items[search_focus].filename;
	if (prev_focus) prev_focus = prev_focus.substr(strlen(MUSIC_DIR), ~0);
	
#ifdef ARLIB_GUI_GTK3
	string key = gtk_entry_get_text(search_line);
#else
	string key = gtk_editable_get_text(GTK_EDITABLE(search_line));
#endif
	key = key.csplit<1>("*")[0];
	
	c_search.reset();
	search_focus = 0;
	if (!key) return;
	
	struct match_t {
		cstring fn;
		int penalty;
		bool operator<(const match_t& other) const {
			if (this->penalty != other.penalty) return this->penalty < other.penalty;
			return string::inatless(this->fn.replace("/","\1"), other.fn.replace("/","\1"));
		};
	};
	array<match_t> matches;
	array<cstring> words = key.csplit(" ");
	
	for (cstring fn : search_filenames)
	{
		int penalty = penalty_for(words, fn);
		if (penalty >= 0)
			matches.append({ fn, penalty });
	}
	
	// heapsort allows me to stop after grabbing the first ~60 items, and not sort the entire 20000-element array
	size_t n_matches_full = matches.size();
	prioqueue<match_t> matches_q(std::move(matches));
	set<cstring> seen_fns;
	while (matches_q.size() && matches.size() <= c_search.capacity)
	{
		match_t m = matches_q.pop();
		cstring real_fn = symlink_filenames.get_or(m.fn, m.fn);
		if (seen_fns.contains(real_fn))
			continue;
		seen_fns.add(real_fn);
		matches.append(m);
	}
	
	c_search.reset();
	if (matches.size() > c_search.capacity)
	{
		int max_penalty = matches[c_search.capacity-1].penalty;
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
	
	if (prev_focus)
	{
		for (size_t i=0;i<matches.size();i++)
		{
			if (matches[i].fn == prev_focus)
			{
				search_focus = i;
				c_search.focus(i);
			}
		}
	}
}

static gboolean search_kb(GtkEventControllerKey* self, guint keyval, guint keycode, GdkModifierType state, void* user_data)
{
	if (keyval == GDK_KEY_Up)
	{
		if (search_focus == 0)
			search_focus = c_search.items.size()-1;
		else
			search_focus--;
		c_search.focus(search_focus);
		return GDK_EVENT_STOP;
	}
	if (keyval == GDK_KEY_Down)
	{
		if (search_focus == c_search.items.size()-1)
			search_focus = 0;
		else
			search_focus++;
		c_search.focus(search_focus);
		return GDK_EVENT_STOP;
	}
	if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)
	{
#ifdef ARLIB_GUI_GTK3
		cstring search_text = gtk_entry_get_text(search_line);
#else
		cstring search_text = gtk_editable_get_text(GTK_EDITABLE(search_line));
#endif
		if (search_text == ".")
		{
			init_search();
			init_common();
		}
		else if (search_text.endswith("*") || search_text.endswith("*?"))
		{
			array<cstring> results;
			
			bool any_good = false;
			array<cstring> words = search_text.crsplit<1>("*")[0].csplit(" ");
			for (column::node& item : c_search.items)
			{
				int penalty = penalty_for(words, item.filename);
				if (penalty < 1000)
					any_good = true;
				if (penalty < 1000 || !any_good)
					results.append(item.filename);
			}
			if (search_text.endswith("*?"))
			{
				for (size_t n=1;n<results.size();n++)
				{
					results.swap(g_rand(n+1), n);
				}
			}
			for (cstring fn : results)
				enqueue_real(fn);
		}
		else if (search_text.startswith("$"))
		{
			enqueue_real(search_text);
		}
		else if (c_search.items)
		{
			enqueue(c_search.items[search_focus].filename);
		}
		if ((state&GDK_SHIFT_MASK) == 0)
		{
#ifdef ARLIB_GUI_GTK3
			gtk_entry_set_text(search_line, "");
#else
			gtk_editable_set_text(GTK_EDITABLE(search_line), ""); // calls remove_cb
#endif
		}
		return GDK_EVENT_STOP;
	}
	if (keyval == GDK_KEY_Escape)
	{
#ifdef ARLIB_GUI_GTK3
		gtk_entry_set_text(search_line, "");
#else
		gtk_editable_set_text(GTK_EDITABLE(search_line), "");
#endif
		return GDK_EVENT_STOP;
	}
	return GDK_EVENT_PROPAGATE;
}
GtkWidget* make_search()
{
	init_search();
	
	search_line = GTK_ENTRY(gtk_entry_new());
#ifdef ARLIB_GUI_GTK3
	auto key_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget)->gboolean
	{
		return search_kb(nullptr, event->key.keyval, event->key.hardware_keycode, (GdkModifierType)event->key.state, nullptr);
	});
	g_signal_connect_swapped(search_line, "key-press-event", G_CALLBACK(key_cb.fp), key_cb.ctx);
#else
	GtkEventController* ctrlkey = gtk_event_controller_key_new();
	gtk_widget_add_controller(GTK_WIDGET(search_line), ctrlkey);
	gtk_event_controller_set_propagation_phase(ctrlkey, GTK_PHASE_CAPTURE);
	g_signal_connect_swapped(ctrlkey, "key-pressed", G_CALLBACK(search_kb), NULL);
#endif
	
	// GTK often passes functions with too few arguments if the latter ones aren't used (for example gtk_widget_hide_on_delete)
	// it works on every ABI I'm aware of, but I'm 99% sure it's undefined behavior per the C++ standard, so I'm not gonna follow suit
	auto update_cb = decompose_lambda([](GParamSpec* pspec, GObject* self)
	{
		update_search();
	});
	g_signal_connect_swapped(gtk_entry_get_buffer(search_line), "notify::text", G_CALLBACK(update_cb.fp), update_cb.ctx);
	
	gtk_widget_set_hexpand(GTK_WIDGET(search_line), true);
	return GTK_WIDGET(search_line);
}


void progress_tick(int pos, int durat);
void stop();
size_t playlist_cur_idx = 0;

void playlist_run()
{
again:
	c_playlist.focus(-1);
	if (playlist_cur_idx == c_playlist.items.size()) { stop(); return; }
	const string& next = c_playlist.items[playlist_cur_idx].filename;
	if (next.startswith("$"))
	{
		(void)! system(1+(const char*)next);
		playlist_cur_idx++;
		goto again;
	}
	c_playlist.focus(playlist_cur_idx);
	xz_finish = []() {
		column::node& n = c_playlist.items[playlist_cur_idx];
		if (n.at != n.total)
		{
			n.at++;
			c_playlist.render(playlist_cur_idx);
		}
		else
			playlist_cur_idx++;
		playlist_run();
	};
	xz_repeat = []() -> bool {
		column::node& n = c_playlist.items[playlist_cur_idx];
		if (n.at != n.total)
		{
			n.at++;
			c_playlist.render(playlist_cur_idx);
			return true;
		}
		else return false;
	};
	xz_progress = progress_tick;
	xz_play(next);
}
void enqueue_real(cstring fn)
{
	if (!fn) return;
	if (playlist_cur_idx && playlist_cur_idx == c_playlist.items.size() && fn == c_playlist.items[playlist_cur_idx-1].filename)
	{
		playlist_cur_idx--;
		c_playlist.items[playlist_cur_idx].at++;
		c_playlist.items[playlist_cur_idx].total++;
		c_playlist.render(playlist_cur_idx);
	}
	else if (!c_playlist.add(fn))
	{
		if (playlist_cur_idx == 0)
			return;
		
		playlist_cur_idx--;
		c_playlist.remove_first();
		c_playlist.focus(playlist_cur_idx);
		c_playlist.add(fn);
	}
	if (!xz_active)
		playlist_run();
}
void enqueue(cstring fn)
{
	int multiple = 1;
#ifdef ARLIB_GUI_GTK3
	const char * mul_str = strchr(gtk_entry_get_text(search_line), '*');
#else
	const char * mul_str = strchr(gtk_editable_get_text(GTK_EDITABLE(search_line)), '*');
#endif
	if (mul_str) fromstring(mul_str+1, multiple);
	if (!multiple) multiple = 1;
	string fn_copy = fn; // in case fn points to the first playlist entry
	for (int i=0;i<multiple;i++)
		enqueue_real(fn_copy);
}


static int progress_prev_pos = -2;

void progress_tick(int pos, int durat)
{
	if (pos < 0) return;
	if (pos == progress_prev_pos)
		return;
#ifdef ARLIB_GUI_GTK3
	if (progress_prev_pos < 0)
		gtk_button_set_image(btn_toggle, btnimg_stop);
#else
	if (progress_prev_pos < 0)
		gtk_button_set_icon_name(btn_toggle, "media-playback-stop");
#endif
	progress_prev_pos = pos;
	
	char buf[32];
	if (durat >= 0)
	{
		sprintf(buf, "%u:%.2u / %u:%.2u", pos/60, pos%60, durat/60, durat%60);
		if (durat == 0)
			gtk_progress_bar_set_fraction(progress, 0.0);
		else
			gtk_progress_bar_set_fraction(progress, (double)pos/durat);
	}
	else
	{
		sprintf(buf, "%u:%.2u / ?:??", pos/60, pos%60);
		gtk_progress_bar_set_fraction(progress, 0.00);
	}
	gtk_progress_bar_set_text(progress, buf);
}

void stop()
{
	xz_stop();
	
	//progress_timer.reset();
	gtk_progress_bar_set_text(progress, "");
	gtk_progress_bar_set_fraction(progress, 0);
	
#ifdef ARLIB_GUI_GTK3
	gtk_button_set_image(btn_toggle, btnimg_play);
#else
	gtk_button_set_icon_name(btn_toggle, "media-playback-start");
#endif
	progress_prev_pos = -2;
}


GtkWidget* make_button(const char * name, void(*cb)())
{
#ifdef ARLIB_GUI_GTK3
	GtkButton* btn = GTK_BUTTON(gtk_button_new_from_icon_name(name, GTK_ICON_SIZE_BUTTON));
#else
	GtkButton* btn = GTK_BUTTON(gtk_button_new_from_icon_name(name));
#endif
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
	if (playlist_cur_idx >= c_playlist.items.size()-1) return;
	playlist_cur_idx++;
	stop();
	playlist_run();
}
void playlist_toggle()
{
	if (xz_active) stop();
	else playlist_run();
}

void make_gui(GApplication* application)
{
	if (mainwnd) return;
	q_fname = g_quark_from_static_string("corn-filename");
	
	if (application) mainwnd = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
	else mainwnd = GTK_WINDOW(gtk_window_new(
#ifdef ARLIB_GUI_GTK3
	GTK_WINDOW_TOPLEVEL
#endif
	));
	
	gtk_window_set_title(mainwnd, "corn");
	
	// the correct solution would be gtk_main_quit + GDK_EVENT_STOP, and only if application==NULL,
	// but the idle handler in ffmpeg somehow keeps the application alive, so let's just grab a bigger toy
#ifdef ARLIB_GUI_GTK3
	auto onclose_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget) -> gboolean { exit(0); });
	g_signal_connect_swapped(mainwnd, "delete-event", G_CALLBACK(onclose_cb.fp), onclose_cb.ctx);
#else
	auto onclose_cb = decompose_lambda([](GtkWindow* self) -> gboolean { exit(0); });
	g_signal_connect_swapped(mainwnd, "close-request", G_CALLBACK(onclose_cb.fp), onclose_cb.ctx);
#endif
	
#ifdef ARLIB_GUI_GTK3
	auto key_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget) -> gboolean
	{
		if (!gtk_widget_is_focus(GTK_WIDGET(search_line)))
			gtk_widget_grab_focus(GTK_WIDGET(search_line));
		return GDK_EVENT_PROPAGATE;
	});
	g_signal_connect_swapped(mainwnd, "key-press-event", G_CALLBACK(key_cb.fp), key_cb.ctx);
#else
	auto onkb_cb = decompose_lambda(
		[](guint keyval, guint keycode, GdkModifierType state, GtkEventControllerKey* self) -> gboolean
	{
		GtkWidget* inner = GTK_WIDGET(gtk_editable_get_delegate(GTK_EDITABLE(search_line)));
		if (!gtk_widget_is_focus(inner))
		{
			gtk_text_grab_focus_without_selecting(GTK_TEXT(inner));
			return gtk_event_controller_key_forward(self, inner);
		}
		return GDK_EVENT_PROPAGATE;
	});
	GtkEventController* ctrlkey = gtk_event_controller_key_new();
	gtk_widget_add_controller(GTK_WIDGET(mainwnd), ctrlkey);
	gtk_event_controller_set_propagation_phase(ctrlkey, GTK_PHASE_CAPTURE);
	g_signal_connect_swapped(ctrlkey, "key-pressed", G_CALLBACK(onkb_cb.fp), onkb_cb.ctx);
#endif
	
#ifdef ARLIB_GUI_GTK3
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
#else
	GtkBox* box_upper = GTK_BOX(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0));
	gtk_box_append(box_upper, make_search());
	gtk_box_append(box_upper, make_button("media-skip-backward", playlist_prev));
	btn_toggle = GTK_BUTTON(make_button("media-playback-start", playlist_toggle));
	gtk_box_append(box_upper, GTK_WIDGET(btn_toggle));
	gtk_box_append(box_upper, make_progress());
	gtk_box_append(box_upper, make_button("media-skip-forward", playlist_next));
#endif
	
	c_common.init(0,2, enqueue);
	c_search.init(2,1, enqueue);
	c_playlist.init(3,1, enqueue);
	grid.init();
	
	GtkBox* box_main = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
#ifdef ARLIB_GUI_GTK3
	gtk_box_pack_start(box_main, GTK_WIDGET(box_upper), false, false, 0);
	gtk_box_pack_start(box_main, GTK_WIDGET(grid.widget), true, true, 0);
	
	gtk_container_add(GTK_CONTAINER(mainwnd), GTK_WIDGET(box_main));
#else
	gtk_box_append(box_main, GTK_WIDGET(box_upper));
	gtk_box_append(box_main, grid.widget);
	
	gtk_window_set_child(mainwnd, GTK_WIDGET(box_main));
#endif
	
	init_common();
	
	gtk_window_set_resizable(mainwnd, true);
	
	gtk_window_set_default_size(mainwnd, 1336, 1026);
#ifdef ARLIB_GUI_GTK3
	gtk_widget_show_all(GTK_WIDGET(mainwnd));
#else
	gtk_widget_show(GTK_WIDGET(mainwnd));
#endif
	
#ifdef ARLIB_GUI_GTK3
	gdk_window_move(gtk_widget_get_window(GTK_WIDGET(mainwnd)), 582, 0);
#else
	// why is gtk like this
	GdkSurface* surf = gtk_native_get_surface(gtk_widget_get_native(GTK_WIDGET(mainwnd)));
	XMoveWindow(gdk_x11_display_get_xdisplay(gdk_surface_get_display(surf)), gdk_x11_surface_get_xid(surf), 582, 0);
#endif
}


void app_open(GApplication* application, GFile* * files, int n_files, char* hint, void* user_data)
{
	make_gui(application);
	for (int i : range(n_files))
	{
		const char * fn = g_file_peek_path(files[i]);
		if (fn)
			enqueue_real(fn);
	}
}

void app_activate(GApplication* application, void* user_data)
{
	make_gui(application);
}

}


#ifdef ARLIB_GUI_GTK4
static bytearray pack_gresource(cstring name, bytesr body)
{
	array<cstring> nameparts = name.cspliti("/");
	array<bytestreamw::pointer> filename_start;
	array<bytestreamw::pointer> body_start;
	array<bytestreamw::pointer> body_end;
	
	bytestreamw by;
	by.text("GVariant"); // signature
	by.u32l(0); // version
	by.u32l(0); // options
	
	by.u32l(0x18); // root directory start
	bytestreamw::pointer root_end = by.ptr32();
	
	by.u32l(0); // bloom size
	by.u32l(1); // hash buckets (bucketing all to the same place is lazy, but for such a small resource pack, it makes no difference)
	
	by.u32l(0); // hash bucket start
	
	uint32_t id = 0;
	uint32_t hash = 5381;
	for (cstring s : nameparts)
	{
		for (uint8_t ch : s.bytes())
		{
			hash = (hash*33) + (int8_t)ch;
		}
		by.u32l(hash);
		by.u32l(id-1); // parent "directory"
		
		filename_start.append(by.ptr32());
		by.u16l(s.length());
		if (id == nameparts.size()-1)
			by.u8('v');
		else
			by.u8('L');
		by.u8(0);
		
		body_start.append(by.ptr32());
		body_end.append(by.ptr32());
		
		id++;
	}
	root_end.fill32l();
	
	id = 0;
	for (cstring s : nameparts)
	{
		if (id == nameparts.size()-1)
			break;
		body_start[id].fill32l();
		by.u32l(id+1);
		body_end[id].fill32l();
		
		id++;
	}
	
	by.align64();
	body_start[id].fill32l();
	by.u32l(body.size());
	by.u32l(0); // flags
	by.bytes(body);
	by.u8(0);
	
	by.u8(0);
	by.text("(uuay)");
	body_end[id].fill32l();
	
	id = 0;
	for (cstring s : nameparts)
	{
		filename_start[id++].fill32l();
		by.strnul(s);
	}
	
	return by.finish();
}
#endif

#include <sys/resource.h>
int main(int argc, char** argv)
{
	arlib_init();
	g_set_prgname("corn");
	
#ifdef ARLIB_GUI_GTK3
	GInputStream* is = g_memory_input_stream_new_from_data(resources::icon, sizeof(resources::icon), NULL);
	GdkPixbuf* pix = gdk_pixbuf_new_from_stream_at_scale(is, 64, 64, true, NULL, NULL);
	gtk_window_set_default_icon(pix);
#else
	bytearray by = pack_gresource("/corn/scalable/apps/corn.svg", resources::icon);
	g_resources_register(g_resource_new_from_data(g_bytes_new(by.ptr(), by.size()), NULL));
	
	gtk_icon_theme_add_resource_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), "/corn/");
	gtk_window_set_default_icon_name("corn");
#endif
	
	// this tool doesn't use Arlib runloop at all
	// g_application_run requires taking every Arlib source and placing in the GLib runloop,
	//  which works badly with Arlib sources being oneshot and O(1) to allocate
	// (it's doable, but pointless. It's easier to just put everything in GLib, it's not like there's many)
	
	if (argv[1] && (cstring)argv[1] == "--local")
	{
		make_gui(NULL);
		char** arg = argv+2;
		while (*arg) enqueue_real(*arg++);
		while (true)
			g_main_context_iteration(nullptr, true);
		return 0;
	}
	
	GtkApplication* app = gtk_application_new("se.muncher.corn", G_APPLICATION_HANDLES_OPEN);
	g_signal_connect(app, "activate", G_CALLBACK(app_activate), nullptr);
	g_signal_connect(app, "open", G_CALLBACK(app_open), nullptr);
	
	return g_application_run(G_APPLICATION(app), argc, argv);
}
