#include "arlib.h"
#include <gtk/gtk.h>
#include <sys/stat.h>

#include <gst/gst.h>

extern "C" { // ffmpeg devs refuse to add this for some unclear reason,
#include <libavformat/avformat.h> // even though more ffmpeg callers seem to be C++ than C,
#include <libavcodec/avcodec.h> // and common.h contains a #if defined(__cplusplus) && etc
#include <libavdevice/avdevice.h>
}

// this tool is designed for personal use only, hardcoded stuff is fine for all intended usecases
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
		
		for (int& foc : focus_row)
			foc = -1;
	}
};
textgrid grid;



GtkWindow* mainwnd = NULL;
GQuark q_fname;

class player_t {
	GstElement* gst_pipeline = NULL;
	guint gst_bus_watch = 0;
	
	bool gst_play(cstring fn)
	{
return false; // segfaults if ffmpeg is loaded into the process
		stop();
		
		gst_pipeline = gst_parse_launch("filesrc location=\""+fn+"\" ! decodebin ! autoaudiosink", NULL);
		if (!gst_pipeline) return false;
puts("playing "+fn+" with gstreamer");
		gst_element_set_state(gst_pipeline, GST_STATE_PLAYING);
		
		GstBus* bus = gst_element_get_bus(gst_pipeline);
		gst_bus_watch = gst_bus_add_watch(bus, [](GstBus* bus, GstMessage* message, void* user_data)->gboolean {
				player_t* this_ = (player_t*)user_data;
				this_->something_cb();
				if (message->type == GST_MESSAGE_EOS)
				{
					this_->gst_bus_watch = 0;
					this_->gst_stop();
					this_->finish_cb();
					return G_SOURCE_REMOVE;
				}
				return G_SOURCE_CONTINUE;
			}, this);
		gst_object_unref(bus);
		
		return true;
	}
	
	void gst_stop()
	{
		if (!gst_pipeline) return;
		gst_element_set_state(gst_pipeline, GST_STATE_NULL);
		gst_object_unref(gst_pipeline);
		gst_pipeline = NULL;
		if (gst_bus_watch != 0) g_source_remove(gst_bus_watch);
	}
	
	bool gst_playing(int* pos, int* durat)
	{
		if (!gst_pipeline) return false;
		
		int64_t song_at;
		int64_t song_len;
		gst_element_query_position(gst_pipeline, GST_FORMAT_TIME, &song_at);
		gst_element_query_duration(gst_pipeline, GST_FORMAT_TIME, &song_len);
		
		if (pos) *pos = GST_CLOCK_TIME_IS_VALID(song_at) ? (song_at+500000000)/1000000000 : -1;
		if (durat) *durat = GST_CLOCK_TIME_IS_VALID(song_len) ? (song_len+500000000)/1000000000 : -1;
		
		return true;
	}
	
	
	
	
	
	
	AVFormatContext* ff_demux = NULL;
	AVCodecContext* ff_codec = NULL;
	AVCodecParameters* ff_par = NULL;
	AVFrame* ff_frame = NULL;
	AVPacket* ff_packet = NULL;
	AVFormatContext* ff_out = NULL;
	array<uint8_t> ff_buf;
	int64_t ff_position;
	int ff_stream_index;
	
	runloop::g_timer ff_idle;
	
	bool ff_play(cstring fn)
	{
		static bool ff_inited = false;
		if (!ff_inited)
		{
			ff_inited = true;
			av_register_all(); // not needed for ffmpeg >= 4.0 (april 2018)
			avcodec_register_all();
			avdevice_register_all();
		}
		
		ff_demux = NULL;
		ff_codec = NULL;
		ff_par = NULL;
		ff_frame = av_frame_alloc();
		ff_packet = av_packet_alloc();
		
		AVCodec* codectype = NULL;
		
		ff_out = avformat_alloc_context();
		ff_out->oformat = av_output_audio_device_next(NULL);
		
		avformat_open_input(&ff_demux, fn.c_str(), NULL, NULL);
		if (!ff_demux)
			goto fail;
		
		avformat_find_stream_info(ff_demux, NULL);
		for (size_t i : range(ff_demux->nb_streams))
		{
			ff_par = ff_demux->streams[i]->codecpar;
			if (ff_par->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				ff_stream_index = i;
				codectype = avcodec_find_decoder(ff_par->codec_id);
				ff_codec = avcodec_alloc_context3(codectype);
				avcodec_parameters_to_context(ff_codec, ff_par);
				avcodec_open2(ff_codec, codectype, NULL);
				break;
			}
		}
		if (!ff_par)
			goto fail;
		
		ff_position = 0;
		
		if (!ff_do_packet(true)) goto fail;
puts("playing "+fn+" with ffmpeg");
		
		ff_idle.set_idle(bind_this(&player_t::ff_cb));
		something_cb();
		
		return true;
		
	fail:
		ff_stop(false);
		return false;
	}
	
	bool ff_do_packet(bool first)
	{
	again:
		if (av_read_frame(ff_demux, ff_packet) < 0) return false;
		
		auto x = dtor([&](){ av_packet_unref(ff_packet); }); // ffmpeg packet memory management is weird
		// and now-inaccurate docs of old ffmpeg versions show up on google and tell me to use av_free_packet, not av_packet_unref
		// https://ffmpeg.org/doxygen/0.11/group__lavf__decoding.html#g4fdb3084415a82e3810de6ee60e46a61
		
		if (ff_packet->stream_index != ff_stream_index) goto again; // discard video frames
		
		avcodec_send_packet(ff_codec, ff_packet);
		avcodec_receive_frame(ff_codec, ff_frame);
		
		if (ff_frame->format == AV_SAMPLE_FMT_NONE) goto again; // rare, but not nonexistent
		
		// av_frame_get_best_effort_timestamp exists, but I can't figure out what unit it uses
		// and anything based on input position is a few seconds (audio buffer size) too high
		ff_position += ff_frame->nb_samples;
		
		static const AVCodecID codecs[] = {
			AV_CODEC_ID_PCM_U8,                                    // AV_SAMPLE_FMT_U8
			AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),   // AV_SAMPLE_FMT_S16
			AV_NE(AV_CODEC_ID_PCM_S32BE, AV_CODEC_ID_PCM_S32LE),   // AV_SAMPLE_FMT_S32
			AV_NE(AV_CODEC_ID_PCM_F32BE, AV_CODEC_ID_PCM_F32LE),   // AV_SAMPLE_FMT_FLT
			AV_NE(AV_CODEC_ID_PCM_F64BE, AV_CODEC_ID_PCM_F64LE),   // AV_SAMPLE_FMT_DBL
			AV_CODEC_ID_PCM_U8,                                    // AV_SAMPLE_FMT_U8P
			AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE),   // AV_SAMPLE_FMT_S16P
			AV_NE(AV_CODEC_ID_PCM_S32BE, AV_CODEC_ID_PCM_S32LE),   // AV_SAMPLE_FMT_S32P
			AV_NE(AV_CODEC_ID_PCM_F32BE, AV_CODEC_ID_PCM_F32LE),   // AV_SAMPLE_FMT_FLTP
			AV_NE(AV_CODEC_ID_PCM_F64BE, AV_CODEC_ID_PCM_F64LE),   // AV_SAMPLE_FMT_DBLP
			AV_NE(AV_CODEC_ID_PCM_S64BE, AV_CODEC_ID_PCM_S64LE),   // AV_SAMPLE_FMT_S64
			AV_NE(AV_CODEC_ID_PCM_S64BE, AV_CODEC_ID_PCM_S64LE),   // AV_SAMPLE_FMT_S64P
		};
		static const uint8_t fmtdescs[] = {
			1,       // AV_SAMPLE_FMT_U8
			2,       // AV_SAMPLE_FMT_S16
			4,       // AV_SAMPLE_FMT_S32
			4,       // AV_SAMPLE_FMT_FLT
			8,       // AV_SAMPLE_FMT_DBL
			1 | 128, // AV_SAMPLE_FMT_U8P
			2 | 128, // AV_SAMPLE_FMT_S16P
			4 | 128, // AV_SAMPLE_FMT_S32P
			4 | 128, // AV_SAMPLE_FMT_FLTP
			8 | 128, // AV_SAMPLE_FMT_DBLP
			8,       // AV_SAMPLE_FMT_S64
			8 | 128, // AV_SAMPLE_FMT_S64P
		};
		
		if (first)
		{
			if (ff_frame->format >= AV_SAMPLE_FMT_NB) return false;
			
			AVStream* outstrm = avformat_new_stream(ff_out, NULL);
			outstrm->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
			outstrm->codecpar->codec_id = codecs[ff_frame->format];
			outstrm->codecpar->channels = ff_par->channels;
			outstrm->codecpar->sample_rate = ff_par->sample_rate;
			if (avformat_write_header(ff_out, NULL) < 0)
				return false;
		}
		
		uint8_t fmtdesc = fmtdescs[ff_frame->format];
		uint8_t bytes_per = fmtdesc&15;
		
		// codec IDs exist for a couple of planar formats, but not all, so deinterleave manually
		// (and the ones that exist aren't supported by ffmpeg alsa backend)
		if (fmtdesc&128)
		{
			ff_buf.reserve(ff_frame->nb_samples * ff_par->channels * bytes_per);
			
			uint8_t** chans = (uint8_t**)ff_frame->data;
			uint8_t* iter = ff_buf.ptr();
			for (size_t s : range(ff_frame->nb_samples))
			for (size_t c : range(ff_par->channels))
			for (size_t b : range(bytes_per))
				*iter++ = chans[c][s*bytes_per+b]; // this could be optimized, but no real point
			
			ff_packet->data = (uint8_t*)ff_buf.ptr();
		}
		else
		{
			ff_packet->data = ff_frame->data[0];
		}
		
		ff_packet->stream_index = 0;
		ff_packet->size = ff_frame->nb_samples * ff_par->channels * bytes_per;
		ff_packet->dts = ff_frame->pkt_dts;
		ff_packet->duration = av_frame_get_pkt_duration(ff_frame);
		if (ff_packet->size) av_write_frame(ff_out, ff_packet);
		
		return true;
	}
	
	void ff_cb()
	{
		if (ff_do_packet(false))
		{
			ff_idle.set_idle(bind_this(&player_t::ff_cb));
		}
		else
		{
			ff_stop();
			finish_cb();
		}
	}
	
	void ff_stop(bool write_trailer = true)
	{
		if (!ff_demux) return;
		
		// av_write_trailer (actually ff_alsa_close) calls snd_pcm_drain, which blocks while the buffer drains (it's always full)
		// it's correct at the end of the song, but a few seconds of latency on the skip button is not quite desirable
		// I don't know if there's any sensible solution without writing my own alsa/pulse backend
		if (write_trailer) av_write_trailer(ff_out);
		
		av_frame_free(&ff_frame);
		av_packet_free(&ff_packet);
		avformat_close_input(&ff_demux); // I'm not sure how ffmpeg defines input and output, but it works
		avformat_close_input(&ff_out); // avformat_close_output doesn't even exist
		avcodec_free_context(&ff_codec);
		ff_buf.reset();
		ff_idle.reset();
	}
	
	bool ff_playing(int* pos, int* durat)
	{
		if (!ff_demux) return false;
		
		if (pos) *pos = ff_position / ff_par->sample_rate;
		if (durat) *durat = ff_demux->duration / AV_TIME_BASE;
		
		return true;
	}
	
public:
	function<void()> something_cb; // dumb name, but I can't think of anything better
	function<void()> finish_cb;
	
	void stop()
	{
		ff_stop();
		gst_stop();
	}
	
	bool play(cstring fn)
	{
		return ff_play(fn) || gst_play(fn);
	}
	
	bool playing(int* pos, int* durat)
	{
		return ff_playing(pos, durat) || gst_playing(pos, durat);
	}
} g_player;

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
	// it works on every ABI I'm aware of, but I'm 99% sure it's undefined behavior per the C++ standard, so I'm not gonna follow suit
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


runloop::g_timer progress_timer;

void progress_tick();
void stop();
size_t playlist_cur_idx = 0;
void playlist_run()
{
	c_playlist.focus(-1);
	if (playlist_cur_idx == c_playlist.n_items) { stop(); return; }
	cstring next = c_playlist.items[playlist_cur_idx];
	c_playlist.focus(playlist_cur_idx);
	g_player.finish_cb = [](){ playlist_cur_idx++; playlist_run(); };
	g_player.something_cb = progress_tick;
	g_player.play(next);
	progress_timer.set_repeat(1000, progress_tick);
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
	if (!g_player.playing(NULL,NULL)) playlist_run();
}


void progress_tick()
{
	int pos;
	int durat;
	if (!g_player.playing(&pos, &durat)) return;
	gtk_button_set_image(btn_toggle, btnimg_stop);
	if (pos < 0) return;
	
	char buf[32];
	if (durat >= 0)
	{
		sprintf(buf, "%u:%.2u / %u:%.2u", pos/60, pos%60, durat/60, durat%60);
		gtk_progress_bar_set_fraction(progress, (double)pos/durat);
	}
	else
		sprintf(buf, "%u:%.2u / ?:??", pos/60, pos%60);
	gtk_progress_bar_set_text(progress, buf);
}

void stop()
{
	g_player.stop();
	
	progress_timer.reset();
	gtk_progress_bar_set_text(progress, "");
	gtk_progress_bar_set_fraction(progress, 0);
	
	gtk_button_set_image(btn_toggle, btnimg_play);
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
	if (g_player.playing(NULL,NULL)) stop();
	else playlist_run();
}

void make_gui(GApplication* application)
{
	if (mainwnd) return;
	q_fname = g_quark_from_static_string("corn-filename");
	
	if (application) mainwnd = GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
	else mainwnd = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
	
	// the correct solution would be gtk_main_quit + GDK_EVENT_STOP, and only if application==NULL,
	// but the idle handler in ffmpeg somehow keeps the application alive, so let's just grab a bigger toy
	auto onclose_cb = decompose_lambda([](GdkEvent* event, GtkWidget* widget) -> gboolean { exit(0); });
	g_signal_connect_swapped(mainwnd, "delete-event", G_CALLBACK(onclose_cb.fp), onclose_cb.ctx);
	
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
	
	if (argv[1] && (cstring)argv[1] == "--local")
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
