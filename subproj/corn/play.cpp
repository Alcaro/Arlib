#include "play.h"

// there has been a gstreamer backend, but it sounds wrong on some songs
// unfortunately, I forgot which, and never wrote it down
// I think they were mp3s, but that doesn't narrow it down much

#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

extern "C" { // ffmpeg devs refuse to add this for some unclear reason,
#include <libavformat/avformat.h> // even though more ffmpeg callers seem to be C++ than C,
#include <libavcodec/avcodec.h> // and common.h contains a #if defined(__cplusplus) && etc
}
#if LIBAVUTIL_VERSION_MAJOR > 56 // they also revise their api way too often
#define channels ch_layout.nb_channels
#endif

bool xz_active = false;
function<void()> xz_finish;
function<bool()> xz_repeat;
function<void(int pos, int durat)> xz_progress;

namespace {

class ffmpeg {
	AVFormatContext* ff_demux = nullptr;
	AVCodecContext* ff_codec = nullptr;
	AVCodecParameters* ff_par = nullptr;
	AVFrame* ff_frame = nullptr;
	AVPacket* ff_packet = nullptr;
	
	array<uint8_t> ff_buf;
	int64_t ff_position;
	int ff_stream_index;
	
	uint8_t* ff_sample_bytes;
	size_t ff_sample_nbytes = 0;
	
public:
	bool play(cstrnul fn)
	{
		stop();
		
		ff_demux = NULL;
		ff_codec = NULL;
		ff_par = NULL;
		ff_frame = av_frame_alloc();
		ff_packet = av_packet_alloc();
		avformat_open_input(&ff_demux, fn, NULL, NULL);
		
		if (!ff_demux)
			goto fail;
		
		avformat_find_stream_info(ff_demux, NULL);
		for (size_t i : range(ff_demux->nb_streams))
		{
			ff_par = ff_demux->streams[i]->codecpar;
			if (ff_par->codec_type == AVMEDIA_TYPE_AUDIO)
			{
				ff_stream_index = i;
				const AVCodec * codectype = avcodec_find_decoder(ff_par->codec_id);
				ff_codec = avcodec_alloc_context3(codectype);
				avcodec_parameters_to_context(ff_codec, ff_par);
				avcodec_open2(ff_codec, codectype, NULL);
				break;
			}
		}
		if (!ff_par || !ff_codec)
			goto fail;
		
		ff_position = 0;
		
		xz_active = true;
		return true;
		
	fail:
		xz_stop();
		return false;
	}
	
	void stop()
	{
		if (!ff_demux)
			return;
		
		av_frame_free(&ff_frame);
		av_packet_free(&ff_packet);
		avformat_close_input(&ff_demux); // I'm not sure how ffmpeg defines input and output, but it works
		avcodec_free_context(&ff_codec);
		ff_buf.reset();
		ff_demux = nullptr;
		ff_sample_nbytes = 0;
		xz_active = false;
	}
	
	AVCodecParameters* get_fmt()
	{
		return ff_par;
	}
	
	void status(int& pos, int& dur)
	{
		pos = ff_position / ff_par->sample_rate;
		dur = ff_demux->duration / AV_TIME_BASE;
	}
	
	bytesr read_packet()
	{
	again:
		if (av_read_frame(ff_demux, ff_packet) < 0)
			return {};
		
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
		
		uint8_t fmtdesc = fmtdescs[ff_frame->format];
		uint8_t bytes_per = fmtdesc&15;
		
		ff_sample_nbytes = ff_frame->nb_samples * ff_par->channels * bytes_per;
		
		// those interleaved formats are just creepy
		if ((fmtdesc&128) && ff_par->channels > 1)
		{
			ff_buf.reserve(ff_sample_nbytes);
			
			uint8_t** chans = (uint8_t**)ff_frame->data;
			uint8_t* iter = ff_buf.ptr();
			for (size_t s : range(ff_frame->nb_samples))
			for (size_t c : range(ff_par->channels))
			for (size_t b : range(bytes_per))
				*iter++ = chans[c][s*bytes_per+b]; // this could be optimized, but no real point
			
			return { ff_buf.ptr(), ff_sample_nbytes };
		}
		else
		{
			return { ff_frame->data[0], ff_sample_nbytes };
		}
	}
	
	void rewind()
	{
		ff_position = 0;
		av_seek_frame(ff_demux, -1, 0, 0);
	}
} ff;

class pulseaudio {
	pa_glib_mainloop* pa_loop = nullptr;
	pa_context* pa_ctx = nullptr;
	pa_stream* pa_strm = nullptr;
	
	pa_sample_spec spec;
	function<void(size_t nbytes)> req_bytes;
	
public:
	pulseaudio()
	{
		pa_loop = pa_glib_mainloop_new(nullptr);
		
		pa_ctx = pa_context_new(pa_glib_mainloop_get_api(pa_loop), "corn");
		pa_context_connect(pa_ctx, nullptr, PA_CONTEXT_NOFLAGS, nullptr);
	}
	
	void begin(const pa_sample_spec * spec, function<void(size_t nbytes)> req_bytes)
	{
		this->spec = *spec;
		this->req_bytes = req_bytes;
		begin();
	}
private:
	void begin()
	{
		cancel();
		
		if (pa_context_get_state(pa_ctx) != PA_CONTEXT_READY)
		{
			pa_context_set_state_callback(pa_ctx, [](pa_context* c, void* userdata) {
				if (pa_context_get_state(c) == PA_CONTEXT_READY)
					((pulseaudio*)userdata)->begin();
			}, this);
			return;
		}
		
		pa_strm = pa_stream_new(pa_ctx, "audio", &spec, NULL);
		pa_stream_set_write_callback(pa_strm,
			[](pa_stream* p, size_t nbytes, void* userdata) { ((pulseaudio*)userdata)->req_bytes(nbytes); }, this);
		pa_stream_connect_playback(pa_strm, nullptr, nullptr, PA_STREAM_NOFLAGS, nullptr, nullptr);
	}
	
public:
	void write(bytesr by)
	{
		pa_stream_write(pa_strm, by.ptr(), by.size(), nullptr, 0, PA_SEEK_RELATIVE);
	}
	
private:
	function<void()> finished;
public:
	void finish(function<void()> finished)
	{
		this->finished = finished;
		pa_stream_set_write_callback(pa_strm, nullptr, nullptr);
		pa_stream_drain(pa_strm, [](pa_stream* s, int success, void* userdata) {
			((pulseaudio*)userdata)->finished();
		}, this);
	}
	
	void cancel()
	{
		if (pa_strm)
		{
			// pulse will occasionally call the write_callback after being disconnected
			// feels like a bug to me, but needs to be worked around, and this is it
			pa_stream_set_write_callback(pa_strm, nullptr, nullptr);
			
			pa_stream_disconnect(pa_strm);
			pa_stream_unref(pa_strm);
			pa_strm = nullptr;
		}
	}
	
	~pulseaudio()
	{
		cancel();
		
		if (pa_ctx)
		{
			pa_context_disconnect(pa_ctx);
			pa_context_unref(pa_ctx);
		}
		
		if (pa_loop)
			pa_glib_mainloop_free(pa_loop);
	}
} pa;

}

bool xz_play(cstrnul fn)
{
	if (!ff.play(fn))
		return false;
	
	static const pa_sample_format codecs[] = {
		PA_SAMPLE_U8,        // AV_SAMPLE_FMT_U8
		PA_SAMPLE_S16NE,     // AV_SAMPLE_FMT_S16
		PA_SAMPLE_S32NE,     // AV_SAMPLE_FMT_S32
		PA_SAMPLE_FLOAT32NE, // AV_SAMPLE_FMT_FLT
		PA_SAMPLE_INVALID,   // AV_SAMPLE_FMT_DBL
		PA_SAMPLE_U8,        // AV_SAMPLE_FMT_U8P
		PA_SAMPLE_S16NE,     // AV_SAMPLE_FMT_S16P
		PA_SAMPLE_S32NE,     // AV_SAMPLE_FMT_S32P
		PA_SAMPLE_FLOAT32NE, // AV_SAMPLE_FMT_FLTP
		PA_SAMPLE_INVALID,   // AV_SAMPLE_FMT_DBLP
		PA_SAMPLE_INVALID,   // AV_SAMPLE_FMT_S64
		PA_SAMPLE_INVALID,   // AV_SAMPLE_FMT_S64P
	};
	
	AVCodecParameters* fmt = ff.get_fmt();
	pa_sample_spec spec = { codecs[fmt->format], (uint32_t)fmt->sample_rate, (uint8_t)fmt->channels };
	pa.begin(&spec, [](size_t nbytes) {
		int pos;
		int dur;
		ff.status(pos, dur);
		xz_progress(pos, dur);
		while ((ssize_t)nbytes > 0)
		{
			bytesr by = ff.read_packet();
			if (by)
			{
				pa.write(by); // this can be more than requested; doesn't matter, it accepts the extra bytes too
				nbytes -= by.size();
			}
			else
			{
				if (xz_repeat())
				{
					ff.rewind();
				}
				else
				{
					ff.stop();
					pa.finish(xz_finish);
				}
				break;
			}
		}
	});
	return true;
}

void xz_stop()
{
	ff.stop();
	pa.cancel();
}
