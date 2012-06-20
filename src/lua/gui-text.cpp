#include "lua/internal.hpp"
#include "fonts/wrapper.hpp"
#include "library/framebuffer.hpp"

namespace
{
	struct render_object_text : public render_object
	{
		render_object_text(int32_t _x, int32_t _y, const std::string& _text, premultiplied_color _fg,
			premultiplied_color _bg, bool _hdbl = false, bool _vdbl = false) throw()
			: x(_x), y(_y), text(_text), fg(_fg), bg(_bg), hdbl(_hdbl), vdbl(_vdbl) {}
		~render_object_text() throw() {}
		template<bool X> void op(struct framebuffer<X>& scr) throw()
		{
			fg.set_palette(scr);
			bg.set_palette(scr);
			main_font.render(scr, x, y, text, fg, bg, hdbl, vdbl);
		}
		void operator()(struct framebuffer<true>& scr) throw()  { op(scr); }
		void operator()(struct framebuffer<false>& scr) throw() { op(scr); }
	private:
		int32_t x;
		int32_t y;
		premultiplied_color fg;
		premultiplied_color bg;
		std::string text;
		bool hdbl;
		bool vdbl;
	};

	int internal_gui_text(lua_State* LS, const std::string& fname, bool hdbl, bool vdbl)
	{
		if(!lua_render_ctx)
			return 0;
		int64_t fgc = 0xFFFFFFU;
		int64_t bgc = -1;
		int32_t _x = get_numeric_argument<int32_t>(LS, 1, fname.c_str());
		int32_t _y = get_numeric_argument<int32_t>(LS, 2, fname.c_str());
		get_numeric_argument<int64_t>(LS, 4, fgc, fname.c_str());
		get_numeric_argument<int64_t>(LS, 5, bgc, fname.c_str());
		std::string text = get_string_argument(LS, 3, fname.c_str());
		premultiplied_color fg(fgc);
		premultiplied_color bg(bgc);
		lua_render_ctx->queue->create_add<render_object_text>(_x, _y, text, fg, bg, hdbl, vdbl);
		return 0;
	}

	function_ptr_luafun gui_text("gui.text", [](lua_State* LS, const std::string& fname) -> int {
		internal_gui_text(LS, fname, false, false);
	});

	function_ptr_luafun gui_textH("gui.textH", [](lua_State* LS, const std::string& fname) -> int {
		internal_gui_text(LS, fname, true, false);
	});

	function_ptr_luafun gui_textV("gui.textV", [](lua_State* LS, const std::string& fname) -> int {
		internal_gui_text(LS, fname, false, true);
	});

	function_ptr_luafun gui_textHV("gui.textHV", [](lua_State* LS, const std::string& fname) -> int {
		internal_gui_text(LS, fname, true, true);
	});
}
