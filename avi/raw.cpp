#include "core/advdumper.hpp"
#include "core/dispatch.hpp"
#include "library/serialization.hpp"

#include <iomanip>
#include <cassert>
#include <cstring>
#include <sstream>
#include <fstream>
#include <zlib.h>

namespace
{
	class raw_avsnoop : public information_dispatch
	{
	public:
		raw_avsnoop(const std::string& prefix) throw(std::bad_alloc)
			: information_dispatch("dump-raw")
		{
			enable_send_sound();
			video = new std::ofstream(prefix + ".video", std::ios::out | std::ios::binary);
			audio = new std::ofstream(prefix + ".audio", std::ios::out | std::ios::binary);
			if(!*video || !*audio)
				throw std::runtime_error("Can't open output files");
			have_dumped_frame = false;
		}

		~raw_avsnoop() throw()
		{
			delete video;
			delete audio;
		}

		void on_frame(struct lcscreen& _frame, uint32_t fps_n, uint32_t fps_d)
		{
			if(!video)
				return;
			unsigned magic = 0x18100800U;
			unsigned r = (reinterpret_cast<unsigned char*>(&magic))[2];
			unsigned g = (reinterpret_cast<unsigned char*>(&magic))[1];
			unsigned b = (reinterpret_cast<unsigned char*>(&magic))[0];
			uint32_t hscl = (_frame.width < 400) ? 2 : 1;
			uint32_t vscl = (_frame.height < 400) ? 2 : 1;
			render_video_hud(dscr, _frame, hscl, vscl, r, g, b, 0, 0, 0, 0, NULL);
			for(size_t i = 0; i < dscr.height; i++)
				video->write(reinterpret_cast<char*>(dscr.rowptr(i)), 4 * dscr.width);
			have_dumped_frame = true;
		}

		void on_sample(short l, short r)
		{
			if(have_dumped_frame && audio) {
				char buffer[4];
				write16sbe(buffer + 0, l);
				write16sbe(buffer + 2, r);
				audio->write(buffer, 4);
			}
		}

		void on_dump_end()
		{
			delete video;
			delete audio;
			video = NULL;
			audio = NULL;
		}

		bool get_dumper_flag() throw()
		{
			return true;
		}
	private:
		std::ofstream* audio;
		std::ofstream* video;
		bool have_dumped_frame;
		struct screen dscr;
	};

	raw_avsnoop* vid_dumper;

	class adv_raw_dumper : public adv_dumper
	{
	public:
		adv_raw_dumper() : adv_dumper("INTERNAL-RAW") {information_dispatch::do_dumper_update(); }
		~adv_raw_dumper() throw();
		std::set<std::string> list_submodes() throw(std::bad_alloc)
		{
			std::set<std::string> x;
			return x;
		}

		bool wants_prefix(const std::string& mode) throw()
		{
			return true;
		}

		std::string name() throw(std::bad_alloc)
		{
			return "RAW";
		}
		
		std::string modename(const std::string& mode) throw(std::bad_alloc)
		{
			return "";
		}

		bool busy()
		{
			return (vid_dumper != NULL);
		}

		void start(const std::string& mode, const std::string& prefix) throw(std::bad_alloc,
			std::runtime_error)
		{
			if(prefix == "")
				throw std::runtime_error("Expected prefix");
			if(vid_dumper)
				throw std::runtime_error("RAW dumping already in progress");
			try {
				vid_dumper = new raw_avsnoop(prefix);
			} catch(std::bad_alloc& e) {
				throw;
			} catch(std::exception& e) {
				std::ostringstream x;
				x << "Error starting RAW dump: " << e.what();
				throw std::runtime_error(x.str());
			}
			messages << "Dumping to " << prefix << std::endl;
			information_dispatch::do_dumper_update();
		}

		void end() throw()
		{
			if(!vid_dumper)
				throw std::runtime_error("No RAW video dump in progress");
			try {
				vid_dumper->on_dump_end();
				messages << "RAW Dump finished" << std::endl;
			} catch(std::bad_alloc& e) {
				throw;
			} catch(std::exception& e) {
				messages << "Error ending RAW dump: " << e.what() << std::endl;
			}
			delete vid_dumper;
			vid_dumper = NULL;
			information_dispatch::do_dumper_update();
		}
	} adv;
	
	adv_raw_dumper::~adv_raw_dumper() throw()
	{
	}
}
