#ifndef _output_cscd__hpp__included__
#define _output_cscd__hpp__included__

#if defined(__linux__) && !defined(BOOST_THREADS)
#define NATIVE_THREADS 1
#endif



#ifdef NATIVE_THREADS
#include <thread>
#include <condition_variable>
#include <mutex>
typedef std::thread thread_class;
typedef std::condition_variable cv_class;
typedef std::mutex mutex_class;
typedef std::unique_lock<std::mutex> umutex_class;
#else
#include <boost/thread.hpp>
#include <boost/thread/locks.hpp>
typedef boost::thread thread_class;
typedef boost::condition_variable cv_class;
typedef boost::mutex mutex_class;
typedef boost::unique_lock<boost::mutex> umutex_class;
#endif

#include <cstdint>
#include <stdexcept>
#include <cstdlib>
#include <vector>
#include <fstream>
#include <list>

struct avi_file_structure;

/**
 * Dump AVI using CSCD for video and PCM for audio.
 */
class avi_cscd_dumper
{
public:
/**
 * AVI dumper parameters.
 */
	struct global_parameters
	{
/**
 * Sound sampling rate.
 */
		unsigned long sampling_rate;
	};

/**
 * Pixel formats
 */
	enum pixelformat
	{
		PIXFMT_RGBX,		/* 32-bit RGB, RGBx order. */
		PIXFMT_BGRX,		/* 32-bit RGB, BGRx order. */
		PIXFMT_XRGB,		/* 32-bit RGB, BGRx order. */
		PIXFMT_XBGR		/* 32-bit RGB, xBGR order. */
	};

/**
 * AVI per-segment parameters
 */
	struct segment_parameters
	{
/**
 * Framerate numerator.
 */
		unsigned long fps_n;
/**
 * Framerate denominator.
 */
		unsigned long fps_d;
/**
 * Pixel format.
 */
		enum pixelformat dataformat;
/**
 * Picture width
 */
		unsigned width;
/**
 * Picture height
 */
		unsigned height;
/**
 * If TRUE, always use default stride (bytes-per-pixel * width)
 */
		bool default_stride;
/**
 * Picture stride in bytes.
 */
		size_t stride;
/**
 * Keyframe distance (1 => every frame is keyframe).
 */
		unsigned keyframe_distance;
/**
 * Deflate compression level (0-9)
 */
		unsigned deflate_level;
/**
 * Maximum number of frames per major segment.
 */
		unsigned long max_segment_frames;

	};
/**
 * Create new dumper.
 *
 * Parameter prefix: Prefix for dumped files.
 * Parameter global: Global dumper parameters.
 * Parameter segment: Dumper segment parameters.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Illegal parameters.
 *
 * Note: Segment parameters have to be sane, but altering those before dumping the first frame does not cause
 * extraneous segment.
 */
	avi_cscd_dumper(const std::string& prefix, const global_parameters& global, const segment_parameters& segment)
		throw(std::bad_alloc, std::runtime_error);

/**
 * Try to close the dump.
 */
	~avi_cscd_dumper() throw();

/**
 * Get current segment parameters.
 *
 * Returns: The segment parameters
 */
	segment_parameters get_segment_parameters() throw();

/**
 * Set segment parameters.
 *
 * Parameter segment: New segment parameters.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Illegal parameters.
 *
 * Note: If parameters change in incompatible manner, next dumped frame will cause segment change. The following
 * changes are incompatible:
 * - Changing the framerate.
 * - Changing data format between 15 and 24/32 bit formats.
 * - Changing with and height.
 */
	void set_segment_parameters(const segment_parameters& segment) throw(std::bad_alloc, std::runtime_error);

/**
 * Dump a frame.
 *
 * Parameter framedata: The frame data, in left-to-right, top-to-bottom order. Pixel format is as specified in
 * segment parameters. If NULL, a black frame is dumped. Needs to be held stable until fully read in MT mode.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Can't write frame.
 */
	void video(const void* framedata) throw(std::bad_alloc, std::runtime_error);

/**
 * Is there frame being processed?
 */
	bool is_frame_processing() throw();

/**
 * Wait until frame has processed.
 */
	void wait_frame_processing() throw();

/**
 * Dump audio.
 *
 * Parameter audio: Audio, first to last channel, first to last sample order.
 * Parameter samples: Number of samples to add. Note: Number of samples, not number of channels*samples.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Can't write sound data.
 */
	void audio(const short* audio, size_t samples) throw(std::bad_alloc, std::runtime_error);

/**
 * Dump audio (stereo).
 *
 * Parameter laudio: Audio for left channel.
 * Parameter raudio: Audio for right channel.
 * Parameter samples: Number of samples to add.
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Can't write sound data or not stereo sound.
 */
	void audio(const short* laudio, const short* raudio, size_t samples) throw(std::bad_alloc, std::runtime_error);

/**
 * Signal end of dump.
 *
 * Throws std::bad_alloc: Not enough memory.
 * Throws std::runtime_error: Can't flush the last segment.
 */
	void end() throw(std::bad_alloc, std::runtime_error);

	int encode_thread();
	void set_capture_error(const std::string& err);
private:
	//Information about buffered frame.
	struct buffered_frame
	{
		std::vector<unsigned char> data;
		bool keyframe;
		unsigned compression_level;
		bool forcebreak;
		unsigned long fps_n;
		unsigned long fps_d;
		unsigned width;
		unsigned height;
	};

	//Global parameters.
	std::string dump_prefix;
	unsigned long gp_sampling_rate;
	//Current segment parameters.
	unsigned long sp_fps_n;
	unsigned long sp_fps_d;
	enum pixelformat sp_dataformat;
	unsigned sp_width;
	unsigned sp_height;
	size_t sp_stride;
	unsigned sp_keyframe_distance;
	unsigned sp_deflate_level;
	unsigned long sp_max_segment_frames;
	//Next segment parameters (some parameters can switch immediately).
	bool switch_segments_on_next_frame;
	unsigned spn_fps_n;
	unsigned spn_fps_d;
	enum pixelformat spn_dataformat;
	unsigned spn_width;
	unsigned spn_height;

	//Current segment.
	unsigned current_major_segment;
	unsigned next_minor_segment;
	unsigned long current_major_segment_frames;
	unsigned frames_since_last_keyframe;
	unsigned long frame_period_counter;
	std::vector<unsigned char> previous_frame;
	std::vector<unsigned char> compression_input;
	std::vector<unsigned char> compression_output;
	avi_file_structure* avifile_structure;
	std::ofstream avifile;

	//Sound&frame buffer.
	std::vector<unsigned short> sound_buffer;
	size_t buffered_sound_samples;
	std::list<buffered_frame> frame_buffer;

	//Fills compression_output with frame/sound packet and returns the total size.
	size_t emit_frame(const std::vector<unsigned char>& data, bool keyframe, unsigned level);
	size_t emit_sound(size_t samples);
	//Write a frame/sound packet in compression_output into stream.
	void emit_frame_stream(size_t size, bool keyframe);
	void emit_sound_stream(size_t size, size_t samples);
	//Read next frame from queue with associated sound of given length. Also handles splits.
	void write_frame_av(size_t samples);

	size_t samples_for_next_frame();
	bool restart_segment_if_needed(bool force_break);
	void flush_buffers(bool forced);
	void start_segment(unsigned major_seg, unsigned minor_seg);
	void end_segment();

	void request_flush_buffers(bool forced);
	void _video(const void* framedata);

	//Multithreading stuff.
	thread_class* frame_thread;
	cv_class frame_cond;
	mutex_class frame_mutex;
	volatile bool quit_requested;
	volatile bool flush_requested;
	volatile bool flush_requested_forced;
	volatile bool frame_processing;
	volatile const void* frame_pointer;
	volatile bool exception_error_present;
	std::string exception_error;
};

#endif
