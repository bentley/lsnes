#include "core/command.hpp"
#include "core/moviedata.hpp"
#include "core/subtitles.hpp"
#include "core/window.hpp"
#include "library/string.hpp"
#include "fonts/wrapper.hpp"

moviefile_subtiming::moviefile_subtiming(uint64_t _frame)
{
	position_only = true;
	frame = _frame;
	length = 0;
}

moviefile_subtiming::moviefile_subtiming(uint64_t first, uint64_t _length)
{
	position_only = false;
	frame = first;
	length = _length;
}

bool moviefile_subtiming::operator<(const moviefile_subtiming& a) const
{
	//This goes in inverse order due to behaviour of lower_bound/upper_bound.
	if(frame > a.frame)
		return true;
	if(frame < a.frame)
		return false;
	if(position_only && a.position_only)
		return false;
	//Position only compares greater than any of same frame.
	if(position_only != a.position_only)
		return position_only;
	//Break ties on length.
	return (length > a.length);
}

bool moviefile_subtiming::operator==(const moviefile_subtiming& a) const
{
	if(frame != a.frame)
		return false;
	if(position_only && a.position_only)
		return true;
	if(position_only != a.position_only)
		return false;
	return (length != a.length);
}

bool moviefile_subtiming::inrange(uint64_t x) const
{
	if(position_only)
		return false;
	return (x >= frame && x < frame + length);
}

uint64_t moviefile_subtiming::get_frame() const { return frame; }
uint64_t moviefile_subtiming::get_length() const { return length; }

namespace
{
	struct render_object_subtitle : public render_object
	{
		render_object_subtitle(int32_t _x, int32_t _y, const std::string& _text) throw()
			: x(_x), y(_y), text(_text), fg(0xFFFF80), bg(-1) {}
		~render_object_subtitle() throw() {}
		template<bool X> void op(struct framebuffer<X>& scr) throw()
		{
			fg.set_palette(scr);
			bg.set_palette(scr);
			main_font.render(scr, x, y, text, fg, bg, false, false);
		}
		void operator()(struct framebuffer<true>& scr) throw()  { op(scr); }
		void operator()(struct framebuffer<false>& scr) throw() { op(scr); }
	private:
		int32_t x;
		int32_t y;
		premultiplied_color fg;
		premultiplied_color bg;
		std::string text;
	};


	function_ptr_command<const std::string&> edit_subtitle("edit-subtitle", "Edit a subtitle",
		"Syntax: edit-subtitle <first> <length> <text>\nAdd/Edit subtitle\n"
		"Syntax: edit-subtitle <first> <length>\nADelete subtitle\n",
		[](const std::string& args) throw(std::bad_alloc, std::runtime_error) {
			auto r = regex("([0-9]+)[ \t]+([0-9]+)([ \t]+(.*))?", args, "Bad syntax");
			uint64_t frame = parse_value<uint64_t>(r[1]);
			uint64_t length = parse_value<uint64_t>(r[2]);
			std::string text = r[4];
			moviefile_subtiming key(frame, length);
			if(text == "")
				our_movie.subtitles.erase(key);
			else
				our_movie.subtitles[key] = s_unescape(text);
		});

	function_ptr_command<> list_subtitle("list-subtitle", "List the subtitles",
		"Syntax: list-subtitle\nList the subtitles.\n",
		[]() throw(std::bad_alloc, std::runtime_error) {
			for(auto i = our_movie.subtitles.rbegin(); i != our_movie.subtitles.rend(); i++) {
				messages << i->first.get_frame() << " " << i->first.get_length() << " "
					<< s_escape(i->second) << std::endl;
			}
		});
}

std::string s_escape(std::string x)
{
	std::string y;
	for(size_t i = 0; i < x.length(); i++) {
		char ch = x[i];
		if(ch == '\n')
			y += "\\n";
		else if(ch == '\\')
			y += "\\";
		else
			y.append(1, ch);
	}
	return y;
}

std::string s_unescape(std::string x)
{
	bool escape = false;
	std::string y;
	for(size_t i = 0; i < x.length(); i++) {
		char ch = x[i];
		if(escape) {
			if(ch == 'n')
				y.append(1, '\n');
			if(ch == '\\')
				y.append(1, '\\');
			escape = false;
		} else {
			if(ch == '\\')
				escape = true;
			else
				y.append(1, ch);
		}
	}
	return y;
}

void render_subtitles(lua_render_context& ctx)
{
	if(our_movie.subtitles.empty())
		return;
	if(ctx.bottom_gap < 32)
		ctx.bottom_gap = 32;
	uint64_t curframe = movb.get_movie().get_current_frame() + 1;
	moviefile_subtiming posmarker(curframe);
	auto i = our_movie.subtitles.upper_bound(posmarker);
	if(i != our_movie.subtitles.end() && i->first.inrange(curframe)) {
		std::string subtxt = i->second;
		int32_t y = ctx.height;
		ctx.queue->create_add<render_object_subtitle>(0, y, subtxt);
	}
}


