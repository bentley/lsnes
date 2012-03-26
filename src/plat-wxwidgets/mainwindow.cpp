#include "lsnes.hpp"

#include "core/command.hpp"
#include "core/controller.hpp"
#include "core/controllerframe.hpp"
#include "core/dispatch.hpp"
#include "core/framebuffer.hpp"
#include "core/framerate.hpp"
#include "core/lua.hpp"
#include "core/mainloop.hpp"
#include "core/memorywatch.hpp"
#include "core/misc.hpp"
#include "core/moviedata.hpp"
#include "core/settings.hpp"
#include "core/window.hpp"
#include "library/string.hpp"
#include "library/zip.hpp"

#include <cmath>
#include <vector>
#include <string>

#include "plat-wxwidgets/menu_dump.hpp"
#include "plat-wxwidgets/platform.hpp"
#include "plat-wxwidgets/window_mainwindow.hpp"
#include "plat-wxwidgets/window_status.hpp"

#define MAXCONTROLLERS MAX_PORTS * MAX_CONTROLLERS_PER_PORT

extern "C"
{
#ifndef UINT64_C
#define UINT64_C(val) val##ULL
#endif
#include <libswscale/swscale.h>
}

enum
{
	wxID_PAUSE = wxID_HIGHEST + 1,
	wxID_FRAMEADVANCE,
	wxID_SUBFRAMEADVANCE,
	wxID_NEXTPOLL,
	wxID_ERESET,
	wxID_AUDIO_ENABLED,
	wxID_SHOW_AUDIO_STATUS,
	wxID_AUDIODEV_FIRST,
	wxID_AUDIODEV_LAST = wxID_AUDIODEV_FIRST + 255,
	wxID_SAVE_STATE,
	wxID_SAVE_MOVIE,
	wxID_LOAD_STATE,
	wxID_LOAD_STATE_RO,
	wxID_LOAD_STATE_RW,
	wxID_LOAD_STATE_P,
	wxID_LOAD_MOVIE,
	wxID_RUN_SCRIPT,
	wxID_RUN_LUA,
	wxID_EVAL_LUA,
	wxID_SAVE_SCREENSHOT,
	wxID_READONLY_MODE,
	wxID_EDIT_AUTHORS,
	wxID_AUTOHOLD_FIRST,
	wxID_AUTOHOLD_LAST = wxID_AUTOHOLD_FIRST + 1023,
	wxID_EDIT_AXES,
	wxID_EDIT_SETTINGS,
	wxID_EDIT_KEYBINDINGS,
	wxID_EDIT_ALIAS,
	wxID_EDIT_MEMORYWATCH,
	wxID_SAVE_MEMORYWATCH,
	wxID_LOAD_MEMORYWATCH,
	wxID_DUMP_FIRST,
	wxID_DUMP_LAST = wxID_DUMP_FIRST + 1023,
	wxID_REWIND_MOVIE,
	wxID_EDIT_JUKEBOX,
	wxID_MEMORY_SEARCH,
	wxID_CANCEL_SAVES,
	wxID_EDIT_HOTKEYS,
	wxID_SHOW_STATUS,
	wxID_SET_SPEED,
	wxID_SET_VOLUME
};


namespace
{
	std::string last_volume = "0dB";
	unsigned char* screen_buffer;
	uint32_t old_width;
	uint32_t old_height;
	bool main_window_dirty;
	struct thread* emulation_thread;

	wxString getname()
	{
		std::string windowname = "lsnes rr" + lsnes_version + "[" + bsnes_core_version + "]";
		return towxstring(windowname);
	}

	struct emu_args
	{
		struct loaded_rom* rom;
		struct moviefile* initial;
		bool load_has_to_succeed;
	};

	void* emulator_main(void* _args)
	{
		struct emu_args* args = reinterpret_cast<struct emu_args*>(_args);
		try {
			our_rom = args->rom;
			struct moviefile* movie = args->initial;
			bool has_to_succeed = args->load_has_to_succeed;
			platform::flush_command_queue();
			main_loop(*our_rom, *movie, has_to_succeed);
			signal_program_exit();
		} catch(std::bad_alloc& e) {
			OOM_panic();
		} catch(std::exception& e) {
			messages << "FATAL: " << e.what() << std::endl;
			platform::fatal_error();
		}
		return NULL;
	}

	void join_emulator_thread()
	{
		emulation_thread->join();
	}

	keygroup mouse_x("mouse_x", keygroup::KT_MOUSE);
	keygroup mouse_y("mouse_y", keygroup::KT_MOUSE);
	keygroup mouse_l("mouse_left", keygroup::KT_KEY);
	keygroup mouse_m("mouse_center", keygroup::KT_KEY);
	keygroup mouse_r("mouse_right", keygroup::KT_KEY);
	keygroup mouse_i("mouse_inwindow", keygroup::KT_KEY);

	void handle_wx_mouse(wxMouseEvent& e)
	{
		platform::queue(keypress(modifier_set(), mouse_x, e.GetX()));
		platform::queue(keypress(modifier_set(), mouse_y, e.GetY()));
		if(e.Entering())
			platform::queue(keypress(modifier_set(), mouse_i, 1));
		if(e.Leaving())
			platform::queue(keypress(modifier_set(), mouse_i, 0));
		if(e.LeftDown())
			platform::queue(keypress(modifier_set(), mouse_l, 1));
		if(e.LeftUp())
			platform::queue(keypress(modifier_set(), mouse_l, 0));
		if(e.MiddleDown())
			platform::queue(keypress(modifier_set(), mouse_m, 1));
		if(e.MiddleUp())
			platform::queue(keypress(modifier_set(), mouse_m, 0));
		if(e.RightDown())
			platform::queue(keypress(modifier_set(), mouse_r, 1));
		if(e.RightUp())
			platform::queue(keypress(modifier_set(), mouse_r, 0));
	}

	bool is_readonly_mode()
	{
		bool ret;
		runemufn([&ret]() { ret = movb.get_movie().readonly_mode(); });
		return ret;
	}

	bool UI_get_autohold(unsigned pid, unsigned idx)
	{
		bool ret;
		runemufn([&ret, pid, idx]() { ret = controls.autohold(pid, idx); });
		return ret;
	}

	void UI_change_autohold(unsigned pid, unsigned idx, bool newstate)
	{
		runemufn([pid, idx, newstate]() { controls.autohold(pid, idx, newstate); });
	}

	int UI_controller_index_by_logical(unsigned lid)
	{
		int ret;
		runemufn([&ret, lid]() { ret = controls.lcid_to_pcid(lid); });
		return ret;
	}

	int UI_button_id(unsigned pcid, unsigned lidx)
	{
		int ret;
		runemufn([&ret, pcid, lidx]() { ret = controls.button_id(pcid, lidx); });
		return ret;
	}

	class controller_autohold_menu : public wxMenu
	{
	public:
		controller_autohold_menu(unsigned lid, enum devicetype_t dtype);
		void change_type();
		bool is_dummy();
		void on_select(wxCommandEvent& e);
		void update(unsigned pid, unsigned ctrlnum, bool newstate);
	private:
		unsigned our_lid;
		wxMenuItem* entries[MAX_LOGICAL_BUTTONS];
		unsigned enabled_entries;
	};

	class autohold_menu : public wxMenu
	{
	public:
		autohold_menu(wxwin_mainwindow* win);
		void reconfigure();
		void on_select(wxCommandEvent& e);
		void update(unsigned pid, unsigned ctrlnum, bool newstate);
	private:
		controller_autohold_menu* menus[MAXCONTROLLERS];
		wxMenuItem* entries[MAXCONTROLLERS];
	};

	class sound_select_menu : public wxMenu
	{
	public:
		sound_select_menu(wxwin_mainwindow* win);
		void update(const std::string& dev);
		void on_select(wxCommandEvent& e);
	private:
		std::map<std::string, wxMenuItem*> items; 
		std::map<int, std::string> devices;
	};

	class sound_select_menu;

	class broadcast_listener : public information_dispatch
	{
	public:
		broadcast_listener(wxwin_mainwindow* win);
		void set_sound_select(sound_select_menu* sdev);
		void set_autohold_menu(autohold_menu* ah);
		void on_sound_unmute(bool unmute) throw();
		void on_sound_change(const std::string& dev) throw();
		void on_mode_change(bool readonly) throw();
		void on_autohold_update(unsigned pid, unsigned ctrlnum, bool newstate);
		void on_autohold_reconfigure();
	private:
		wxwin_mainwindow* mainw;
		sound_select_menu* sounddev;
		autohold_menu* ahmenu;
	};

	controller_autohold_menu::controller_autohold_menu(unsigned lid, enum devicetype_t dtype)
	{
		modal_pause_holder hld;
		our_lid = lid;
		for(unsigned i = 0; i < MAX_LOGICAL_BUTTONS; i++) {
			int id = wxID_AUTOHOLD_FIRST + MAX_LOGICAL_BUTTONS * lid + i;
			entries[i] = AppendCheckItem(id, towxstring(get_logical_button_name(i)));
		}
		change_type();
	}

	void controller_autohold_menu::change_type()
	{
		enabled_entries = 0;
		int pid = controls.lcid_to_pcid(our_lid);
		for(unsigned i = 0; i < MAX_LOGICAL_BUTTONS; i++) {
			int pidx = -1;
			if(pid >= 0)
				pidx = controls.button_id(pid, i);
			if(pidx >= 0) {
				entries[i]->Check(pid > 0 && UI_get_autohold(pid, pidx));
				entries[i]->Enable();
				enabled_entries++;
			} else {
				entries[i]->Check(false);
				entries[i]->Enable(false);
			}
		}
	}

	bool controller_autohold_menu::is_dummy()
	{
		return !enabled_entries;
	}

	void controller_autohold_menu::on_select(wxCommandEvent& e)
	{
		int x = e.GetId();
		if(x < wxID_AUTOHOLD_FIRST + our_lid * MAX_LOGICAL_BUTTONS || x >= wxID_AUTOHOLD_FIRST * 
			(our_lid + 1) * MAX_LOGICAL_BUTTONS) {
			return;
		}
		unsigned lidx = (x - wxID_AUTOHOLD_FIRST) % MAX_LOGICAL_BUTTONS;
		modal_pause_holder hld;
		int pid = controls.lcid_to_pcid(our_lid);
		if(pid < 0 || !entries[lidx])
			return;
		int pidx = controls.button_id(pid, lidx);
		if(pidx < 0)
			return;
		//Autohold change on pid=pid, ctrlindx=idx, state
		bool newstate = entries[lidx]->IsChecked();
		UI_change_autohold(pid, pidx, newstate);
	}

	void controller_autohold_menu::update(unsigned pid, unsigned ctrlnum, bool newstate)
	{
		modal_pause_holder hld;
		int pid2 = UI_controller_index_by_logical(our_lid);
		if(pid2 < 0 || static_cast<unsigned>(pid) != pid2)
			return;
		for(unsigned i = 0; i < MAX_LOGICAL_BUTTONS; i++) {
			int idx = UI_button_id(pid2, i);
			if(idx < 0 || static_cast<unsigned>(idx) != ctrlnum)
				continue;
			entries[i]->Check(newstate);
		}
	}


	autohold_menu::autohold_menu(wxwin_mainwindow* win)
	{
		for(unsigned i = 0; i < MAXCONTROLLERS; i++) {
			std::ostringstream str;
			str << "Controller #&" << (i + 1);
			menus[i] = new controller_autohold_menu(i, DT_NONE);
			entries[i] = AppendSubMenu(menus[i], towxstring(str.str()));
			entries[i]->Enable(!menus[i]->is_dummy());
		}
		win->Connect(wxID_AUTOHOLD_FIRST, wxID_AUTOHOLD_LAST, wxEVT_COMMAND_MENU_SELECTED,
			wxCommandEventHandler(autohold_menu::on_select), NULL, this);
		reconfigure();
	}

	void autohold_menu::reconfigure()
	{
		modal_pause_holder hld;
		for(unsigned i = 0; i < MAXCONTROLLERS; i++) {
			menus[i]->change_type();
			entries[i]->Enable(!menus[i]->is_dummy());
		}
	}

	void autohold_menu::on_select(wxCommandEvent& e)
	{
		for(unsigned i = 0; i < MAXCONTROLLERS; i++)
			menus[i]->on_select(e);
	}

	void autohold_menu::update(unsigned pid, unsigned ctrlnum, bool newstate)
	{
		for(unsigned i = 0; i < MAXCONTROLLERS; i++)
			menus[i]->update(pid, ctrlnum, newstate);
	}

	sound_select_menu::sound_select_menu(wxwin_mainwindow* win)
	{
		std::string curdev = platform::get_sound_device();
		int j = wxID_AUDIODEV_FIRST;
		for(auto i : platform::get_sound_devices()) {
			items[i.first] = AppendRadioItem(j, towxstring(i.first + "(" + i.second + ")"));
			devices[j] = i.first;
			if(i.first == curdev)
				items[i.first]->Check();
			win->Connect(j, wxEVT_COMMAND_MENU_SELECTED,
				wxCommandEventHandler(sound_select_menu::on_select), NULL, this);
			j++;
		}
	}

	void sound_select_menu::update(const std::string& dev)
	{
		items[dev]->Check();
	}

	void sound_select_menu::on_select(wxCommandEvent& e)
	{
		std::string devname = devices[e.GetId()];
		if(devname != "")
			runemufn([devname]() { platform::set_sound_device(devname); });
	}

	broadcast_listener::broadcast_listener(wxwin_mainwindow* win)
		: information_dispatch("wxwidgets-broadcast-listener")
	{
		mainw = win;
	}

	void broadcast_listener::set_sound_select(sound_select_menu* sdev)
	{
		sounddev = sdev;
	}

	void broadcast_listener::set_autohold_menu(autohold_menu* ah)
	{
		ahmenu = ah;
	}

	void broadcast_listener::on_sound_unmute(bool unmute) throw()
	{
		runuifun([unmute, mainw]() { mainw->menu_check(wxID_AUDIO_ENABLED, unmute); });
	}

	void broadcast_listener::on_sound_change(const std::string& dev) throw()
	{
		runuifun([dev, sounddev]() { if(sounddev) sounddev->update(dev); });
	}

	void broadcast_listener::on_mode_change(bool readonly) throw()
	{
		runuifun([readonly, mainw]() { mainw->menu_check(wxID_READONLY_MODE, readonly); });
	}

	void broadcast_listener::on_autohold_update(unsigned pid, unsigned ctrlnum, bool newstate)
	{
		runuifun([pid, ctrlnum, newstate, ahmenu]() { ahmenu->update(pid, ctrlnum, newstate); });
	}

	void broadcast_listener::on_autohold_reconfigure()
	{
		runuifun([ahmenu]() { ahmenu->reconfigure(); });
	}
}

void boot_emulator(loaded_rom& rom, moviefile& movie)
{
	try {
		struct emu_args* a = new emu_args;
		a->rom = &rom;
		a->initial = &movie;
		a->load_has_to_succeed = false;
		modal_pause_holder hld;
		emulation_thread = &thread::create(emulator_main, a);
		main_window = new wxwin_mainwindow();
		main_window->Show();
	} catch(std::bad_alloc& e) {
		OOM_panic();
	}
}

wxwin_mainwindow::panel::panel(wxWindow* win)
	: wxPanel(win)
{
	this->Connect(wxEVT_PAINT, wxPaintEventHandler(panel::on_paint), NULL, this);
	this->Connect(wxEVT_ERASE_BACKGROUND, wxEraseEventHandler(panel::on_erase), NULL, this);
	this->Connect(wxEVT_KEY_DOWN, wxKeyEventHandler(panel::on_keyboard_down), NULL, this);
	this->Connect(wxEVT_KEY_UP, wxKeyEventHandler(panel::on_keyboard_up), NULL, this);
	this->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_LEFT_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MIDDLE_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MIDDLE_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_RIGHT_DOWN, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_RIGHT_UP, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_MOTION, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_ENTER_WINDOW, wxMouseEventHandler(panel::on_mouse), NULL, this);
	this->Connect(wxEVT_LEAVE_WINDOW, wxMouseEventHandler(panel::on_mouse), NULL, this);
	SetMinSize(wxSize(512, 448));
}

void wxwin_mainwindow::menu_start(wxString name)
{
	while(!upper.empty())
		upper.pop();
	current_menu = new wxMenu();
	menubar->Append(current_menu, name);
}

void wxwin_mainwindow::menu_special(wxString name, wxMenu* menu)
{
	while(!upper.empty())
		upper.pop();
	menubar->Append(menu, name);
	current_menu = NULL;
}

void wxwin_mainwindow::menu_special_sub(wxString name, wxMenu* menu)
{
	current_menu->AppendSubMenu(menu, name);
}

void wxwin_mainwindow::menu_entry(int id, wxString name)
{
	current_menu->Append(id, name);
	Connect(id, wxEVT_COMMAND_MENU_SELECTED, 
		wxCommandEventHandler(wxwin_mainwindow::wxwin_mainwindow::handle_menu_click), NULL, this);
}

void wxwin_mainwindow::menu_entry_check(int id, wxString name)
{
	checkitems[id] = current_menu->AppendCheckItem(id, name);
	Connect(id, wxEVT_COMMAND_MENU_SELECTED, 
		wxCommandEventHandler(wxwin_mainwindow::wxwin_mainwindow::handle_menu_click), NULL, this);
}

void wxwin_mainwindow::menu_start_sub(wxString name)
{
	wxMenu* old = current_menu;
	upper.push(current_menu);
	current_menu = new wxMenu();
	old->AppendSubMenu(current_menu, name);
}

void wxwin_mainwindow::menu_end_sub(wxString name)
{
	current_menu = upper.top();
	upper.pop();
}

bool wxwin_mainwindow::menu_ischecked(int id)
{
	if(checkitems.count(id))
		return checkitems[id]->IsChecked();
	else
		return false;
}

void wxwin_mainwindow::menu_check(int id, bool newstate)
{
	if(checkitems.count(id))
		return checkitems[id]->Check(newstate);
	else
		return;
}

void wxwin_mainwindow::menu_separator()
{
	current_menu->AppendSeparator();
}

void wxwin_mainwindow::panel::request_paint()
{
	Refresh();
}

void wxwin_mainwindow::panel::on_paint(wxPaintEvent& e)
{
	render_framebuffer();
	static struct SwsContext* ctx;
	uint8_t* srcp[1];
	int srcs[1];
	uint8_t* dstp[1];
	int dsts[1];
	wxPaintDC dc(this);
	if(!screen_buffer || main_screen.width != old_width || main_screen.height != old_height) {
		if(screen_buffer)
			delete[] screen_buffer;
		screen_buffer = new unsigned char[main_screen.width * main_screen.height * 3];
		old_height = main_screen.height;
		old_width = main_screen.width;
		uint32_t w = main_screen.width;
		uint32_t h = main_screen.height;
		if(w && h)
			ctx = sws_getCachedContext(ctx, w, h, PIX_FMT_RGBA, w, h, PIX_FMT_BGR24, SWS_POINT |
				SWS_CPU_CAPS_MMX2, NULL, NULL, NULL);
		if(w < 512)
			w = 512;
		if(h < 448)
			h = 448;
		SetMinSize(wxSize(w, h));
		main_window->Fit();
	}
	srcs[0] = 4 * main_screen.width;
	dsts[0] = 3 * main_screen.width;
	srcp[0] = reinterpret_cast<unsigned char*>(main_screen.memory);
	dstp[0] = screen_buffer;
	memset(screen_buffer, 0, main_screen.width * main_screen.height * 3);
	uint64_t t1 = get_utime();
	if(main_screen.width && main_screen.height)
		sws_scale(ctx, srcp, srcs, 0, main_screen.height, dstp, dsts);
	uint64_t t2 = get_utime();
	wxBitmap bmp(wxImage(main_screen.width, main_screen.height, screen_buffer, true));
	uint64_t t3 = get_utime();
	dc.DrawBitmap(bmp, 0, 0, false);
	main_window_dirty = false;
}

void wxwin_mainwindow::panel::on_erase(wxEraseEvent& e)
{
	//Blank.
}

void wxwin_mainwindow::panel::on_keyboard_down(wxKeyEvent& e)
{
	handle_wx_keyboard(e, true);
}

void wxwin_mainwindow::panel::on_keyboard_up(wxKeyEvent& e)
{
	handle_wx_keyboard(e, false);
}

void wxwin_mainwindow::panel::on_mouse(wxMouseEvent& e)
{
	handle_wx_mouse(e);
}

wxwin_mainwindow::wxwin_mainwindow()
	: wxFrame(NULL, wxID_ANY, getname(), wxDefaultPosition, wxSize(-1, -1),
		wxMINIMIZE_BOX | wxSYSTEM_MENU | wxCAPTION | wxCLIP_CHILDREN | wxCLOSE_BOX)
{
	broadcast_listener* blistener = new broadcast_listener(this);
	Centre();
	toplevel = new wxFlexGridSizer(1, 2, 0, 0);
	toplevel->Add(gpanel = new panel(this), 1, wxGROW);
	toplevel->Add(spanel = new wxwin_status::panel(this, 20), 1, wxGROW);
	spanel_shown = true;
	toplevel->SetSizeHints(this);
	SetSizer(toplevel);
	Fit();
	gpanel->SetFocus();
	Connect(wxEVT_CLOSE_WINDOW, wxCloseEventHandler(wxwin_mainwindow::on_close));
	menubar = new wxMenuBar;
	SetMenuBar(menubar);

	//TOP-level accels: ACFOS.
	//System menu: (ACFOS)EMNPQRU
	menu_start(wxT("&System"));
	menu_entry(wxID_FRAMEADVANCE, wxT("Fra&me advance"));
	menu_entry(wxID_SUBFRAMEADVANCE, wxT("S&ubframe advance"));
	menu_entry(wxID_NEXTPOLL, wxT("&Next poll"));
	menu_entry(wxID_PAUSE, wxT("&Pause/Unpause"));
	menu_separator();
	menu_entry(wxID_ERESET, wxT("&Reset"));
	menu_separator();
	menu_entry(wxID_EDIT_AUTHORS, wxT("&Edit game name && authors"));
	menu_separator();
	menu_entry(wxID_EXIT, wxT("&Quit"));
	menu_separator();
	menu_entry(wxID_ABOUT, wxT("About"));
	//File menu: (ACFOS)DEILMNPRTUVW
	menu_start(wxT("&File"));
	menu_entry_check(wxID_READONLY_MODE, wxT("Reado&nly mode"));
	menu_check(wxID_READONLY_MODE, is_readonly_mode());
	menu_separator();
	menu_entry(wxID_SAVE_STATE, wxT("Save stat&e"));
	menu_entry(wxID_SAVE_MOVIE, wxT("Sa&ve movie"));
	menu_separator();
	menu_entry(wxID_LOAD_STATE, wxT("&Load state"));
	menu_entry(wxID_LOAD_STATE_RO, wxT("Loa&d state (readonly)"));
	menu_entry(wxID_LOAD_STATE_RW, wxT("Load s&tate (read-write)"));
	menu_entry(wxID_LOAD_STATE_P, wxT("Load state (&preserve)"));
	menu_entry(wxID_LOAD_MOVIE, wxT("Load &movie"));
	menu_entry(wxID_REWIND_MOVIE, wxT("Re&wind movie"));
	menu_separator();
	menu_entry(wxID_CANCEL_SAVES, wxT("Cancel pend&ing saves"));
	menu_separator();
	menu_entry(wxID_SAVE_SCREENSHOT, wxT("Save sc&reenshot"));
	menu_separator();
	menu_special_sub(wxT("D&ump video"), reinterpret_cast<dumper_menu*>(dmenu = new dumper_menu(this,
		wxID_DUMP_FIRST, wxID_DUMP_LAST)));
	//Autohold menu: (ACFOS)
	menu_special(wxT("&Autohold"), reinterpret_cast<autohold_menu*>(ahmenu = new autohold_menu(this)));
	blistener->set_autohold_menu(reinterpret_cast<autohold_menu*>(ahmenu));
	//Scripting menu: (ACFOS)ERU
	menu_start(wxT("S&cripting"));
	menu_entry(wxID_RUN_SCRIPT, wxT("&Run script"));
	if(lua_supported) {
		menu_separator();
		menu_entry(wxID_EVAL_LUA, wxT("&Evaluate Lua statement"));
		menu_entry(wxID_RUN_LUA, wxT("R&un Lua script"));
	}
	menu_separator();
	menu_entry(wxID_EDIT_MEMORYWATCH, wxT("Edit memory watch"));
	menu_separator();
	menu_entry(wxID_LOAD_MEMORYWATCH, wxT("Load memory watch"));
	menu_entry(wxID_SAVE_MEMORYWATCH, wxT("Save memory watch"));
	menu_separator();
	menu_entry(wxID_MEMORY_SEARCH, wxT("Memory Search"));
	//Settings menu: (ACFOS)
	menu_start(wxT("Settings"));
	menu_entry(wxID_EDIT_AXES, wxT("Configure axes"));
	menu_entry(wxID_EDIT_SETTINGS, wxT("Configure settings"));
	menu_entry(wxID_EDIT_HOTKEYS, wxT("Configure hotkeys"));
	menu_entry(wxID_EDIT_KEYBINDINGS, wxT("Configure keybindings"));
	menu_entry(wxID_EDIT_ALIAS, wxT("Configure aliases"));
	menu_entry(wxID_EDIT_JUKEBOX, wxT("Configure jukebox"));
	menu_separator();
	menu_entry_check(wxID_SHOW_STATUS, wxT("Show status panel"));
	menu_entry(wxID_SET_SPEED, wxT("Set Speed"));
	menu_check(wxID_SHOW_STATUS, true);
	if(platform::sound_initialized()) {
		//Sound menu: (ACFOS)EHU
		menu_start(wxT("S&ound"));
		menu_entry_check(wxID_AUDIO_ENABLED, wxT("So&unds enabled"));
		menu_check(wxID_AUDIO_ENABLED, platform::is_sound_enabled());
		menu_entry(wxID_SET_VOLUME, wxT("Set Sound volume"));
		menu_entry(wxID_SHOW_AUDIO_STATUS, wxT("S&how audio status"));
		menu_separator();
		menu_special_sub(wxT("S&elect sound device"), reinterpret_cast<sound_select_menu*>(sounddev =
			new sound_select_menu(this)));
		blistener->set_sound_select(reinterpret_cast<sound_select_menu*>(sounddev));
	}
}

void wxwin_mainwindow::request_paint()
{
	gpanel->Refresh();
}

void wxwin_mainwindow::on_close(wxCloseEvent& e)
{
	//Veto it for now, latter things will delete it.
	e.Veto();
	platform::queue("quit-emulator");
}

void wxwin_mainwindow::notify_update() throw()
{
	if(!main_window_dirty) {
		main_window_dirty = true;
		gpanel->Refresh();
	}
}

void wxwin_mainwindow::notify_update_status() throw()
{
	if(!spanel->dirty) {
		spanel->dirty = true;
		spanel->Refresh();
	}
}

void wxwin_mainwindow::notify_exit() throw()
{
	join_emulator_thread();
	Destroy();
}

#define NEW_KEYBINDING "A new binding..."
#define NEW_ALIAS "A new alias..."
#define NEW_WATCH "A new watch..."

void wxwin_mainwindow::handle_menu_click(wxCommandEvent& e)
{
	try {
		handle_menu_click_cancelable(e);
	} catch(canceled_exception& e) {
		//Ignore.
	} catch(std::bad_alloc& e) {
		OOM_panic();
	} catch(std::exception& e) {
		show_message_ok(this, "Error in menu handler", e.what(), wxICON_EXCLAMATION);
	}
}

void wxwin_mainwindow::handle_menu_click_cancelable(wxCommandEvent& e)
{
	std::string filename;
	bool s;
	switch(e.GetId()) {
	case wxID_FRAMEADVANCE:
		platform::queue("+advance-frame");
		platform::queue("-advance-frame");
		return;
	case wxID_SUBFRAMEADVANCE:
		platform::queue("+advance-poll");
		platform::queue("-advance-poll");
		return;
	case wxID_NEXTPOLL:
		platform::queue("advance-skiplag");
		return;
	case wxID_PAUSE:
		platform::queue("pause-emulator");
		return;
	case wxID_ERESET:
		platform::queue("reset");
		return;
	case wxID_EXIT:
		platform::queue("quit-emulator");
		return;
	case wxID_AUDIO_ENABLED:
		platform::sound_enable(menu_ischecked(wxID_AUDIO_ENABLED));
		return;
	case wxID_SHOW_AUDIO_STATUS:
		platform::queue("show-sound-status");
		return;
	case wxID_CANCEL_SAVES:
		platform::queue("cancel-saves");
		return;
	case wxID_LOAD_MOVIE:
		platform::queue("load-movie " + pick_file(this, "Load Movie", "."));
		return;
	case wxID_LOAD_STATE:
		platform::queue("load " + pick_file(this, "Load State", "."));
		return;
	case wxID_LOAD_STATE_RO:
		platform::queue("load-readonly " + pick_file(this, "Load State (Read-Only)", "."));
		return;
	case wxID_LOAD_STATE_RW:
		platform::queue("load-state " + pick_file(this, "Load State (Read-Write)", "."));
		return;
	case wxID_LOAD_STATE_P:
		platform::queue("load-preserve " + pick_file(this, "Load State (Preserve)", "."));
		return;
	case wxID_REWIND_MOVIE:
		platform::queue("rewind-movie");
		return;
	case wxID_SAVE_MOVIE:
		platform::queue("save-movie " + pick_file(this, "Save Movie", "."));
		return;
	case wxID_SAVE_STATE:
		platform::queue("save-state " + pick_file(this, "Save State", "."));
		return;
	case wxID_SAVE_SCREENSHOT:
		platform::queue("take-screenshot " + pick_file(this, "Save State", "."));
		return;
	case wxID_RUN_SCRIPT:
		platform::queue("run-script " + pick_file_member(this, "Select Script", "."));
		return;
	case wxID_RUN_LUA:
		platform::queue("run-lua " + pick_file(this, "Select Lua Script", "."));
		return;
	case wxID_EVAL_LUA:
		platform::queue("evaluate-lua " + pick_text(this, "Evaluate Lua", "Enter Lua Statement:"));
		return;
	case wxID_READONLY_MODE:
		s = menu_ischecked(wxID_READONLY_MODE);
		runemufn([s]() {
			movb.get_movie().readonly_mode(s);
			if(!s)
				lua_callback_do_readwrite();
			update_movie_state();
		});
		return;
	case wxID_EDIT_AXES:
		wxeditor_axes_display(this);
		return;
	case wxID_EDIT_AUTHORS:
		wxeditor_authors_display(this);
		return;
	case wxID_EDIT_SETTINGS:
		wxeditor_settings_display(this);
		return;
	case wxID_EDIT_HOTKEYS:
		wxeditor_hotkeys_display(this);
		return;
	case wxID_EDIT_KEYBINDINGS: {
		modal_pause_holder hld;
		std::set<std::string> bind;
		runemufn([&bind]() { bind = keymapper::get_bindings(); });
		std::vector<std::string> choices;
		choices.push_back(NEW_KEYBINDING);
		for(auto i : bind)
			choices.push_back(i);
		std::string key = pick_among(this, "Select binding", "Select keybinding to edit", choices);
		if(key == NEW_KEYBINDING)
			key = wxeditor_keyselect(this, false);
		std::string old_command_value;
		runemufn([&old_command_value, key]() { old_command_value = keymapper::get_command_for(key); });
		std::string newcommand = pick_text(this, "Edit binding", "Enter new command for binding:",
			old_command_value);
		bool fault = false;
		std::string faulttext;
		runemufn([&fault, &faulttext, key, newcommand]() {
			try {
				keymapper::bind_for(key, newcommand);
			} catch(std::exception& e) {
				fault = true;
				faulttext = e.what();
			}
		});
		if(fault)
			show_message_ok(this, "Error", "Can't bind key: " + faulttext, wxICON_EXCLAMATION);
		return;
	}
	case wxID_EDIT_ALIAS: {
		modal_pause_holder hld;
		std::set<std::string> bind;
		runemufn([&bind]() { bind = command::get_aliases(); });
		std::vector<std::string> choices;
		choices.push_back(NEW_ALIAS);
		for(auto i : bind)
			choices.push_back(i);
		std::string alias = pick_among(this, "Select alias", "Select alias to edit", choices);
		if(alias == NEW_ALIAS) {
			alias = pick_text(this, "Enter alias name", "Enter name for the new alias:");
			if(!command::valid_alias_name(alias)) {
				show_message_ok(this, "Error", "Not a valid alias name: " + alias,
					wxICON_EXCLAMATION);
				throw canceled_exception();
			}
		}
		std::string old_alias_value;
		runemufn([alias, &old_alias_value]() { old_alias_value = command::get_alias_for(alias); });
		std::string newcmd = pick_text(this, "Edit alias", "Enter new commands for alias:",
			old_alias_value, true);
		runemufn([alias, newcmd]() { command::set_alias_for(alias, newcmd); });
		return;
	}
	case wxID_EDIT_JUKEBOX: {
		modal_pause_holder hld;
		std::vector<std::string> new_jukebox;
		std::string x;
		runemufn([&x]() {
			for(auto i : get_jukebox_names())
				x = x + i + "\n";
		});
		x = pick_text(this, "Configure jukebox", "List jukebox entries", x, true);
		while(x != "") {
			size_t split = x.find_first_of("\n");
			std::string l;
			if(split < x.length()) {
				l = x.substr(0, split);
				x = x.substr(split + 1);
			} else {
				l = x;
				x = "";
			}
			istrip_CR(l);
			if(l != "")
				new_jukebox.push_back(l);
		}
		runemufn([&new_jukebox]() { set_jukebox_names(new_jukebox); });
		notify_update_status();
		return;
	}
	case wxID_EDIT_MEMORYWATCH: {
		modal_pause_holder hld;
		std::set<std::string> bind;
		runemufn([&bind]() { bind = get_watches(); });
		std::vector<std::string> choices;
		choices.push_back(NEW_WATCH);
		for(auto i : bind)
			choices.push_back(i);
		std::string watch = pick_among(this, "Select watch", "Select watch to edit", choices);
		if(watch == NEW_WATCH)
			watch =  pick_text(this, "Enter watch name", "Enter name for the new watch:");
		std::string newexpr = pick_text(this, "Edit watch", "Enter new expression for watch:",
			get_watchexpr_for(watch));
		runemufn([watch, newexpr]() { set_watchexpr_for(watch, newexpr); });
		return;
	}
	case wxID_SAVE_MEMORYWATCH: {
		modal_pause_holder hld;
		std::set<std::string> old_watches;
		runemufn([&old_watches]() { old_watches = get_watches(); });
		std::string filename = pick_file(this, "Save watches to file", ".");
		std::ofstream out(filename.c_str());
		for(auto i : old_watches) {
			std::string val;
			runemufn([i, &val]() { val = get_watchexpr_for(i); });
			out << i << std::endl << val << std::endl;
		}
		out.close();
		return;
	}
	case wxID_LOAD_MEMORYWATCH: {
		modal_pause_holder hld;
		std::set<std::string> old_watches;
		runemufn([&old_watches]() { old_watches = get_watches(); });
		std::map<std::string, std::string> new_watches;
		std::string filename = pick_file_member(this, "Choose memory watch file", ".");

		try {
			std::istream& in = open_file_relative(filename, "");
			while(in) {
				std::string wname;
				std::string wexpr;
				std::getline(in, wname);
				std::getline(in, wexpr);
				new_watches[wname] = wexpr;
			}
			delete &in;
		} catch(std::exception& e) {
			show_message_ok(this, "Error", std::string("Can't load memory watch: ") + e.what(),
				wxICON_EXCLAMATION);
			return;
		}

		runemufn([&new_watches, &old_watches]() {
			for(auto i : new_watches)
				set_watchexpr_for(i.first, i.second);
			for(auto i : old_watches)
				if(!new_watches.count(i))
					set_watchexpr_for(i, "");
			});
		return;
	}
	case wxID_MEMORY_SEARCH:
		wxwindow_memorysearch_display();
		return;
	case wxID_ABOUT: {
		std::ostringstream str;
		str << "Version: lsnes rr" << lsnes_version << std::endl;
		str << "Revision: " << lsnes_git_revision << std::endl;
		str << "Core: " << bsnes_core_version << std::endl;
		wxMessageBox(towxstring(str.str()), _T("About"), wxICON_INFORMATION | wxOK, this);
		return;
	}
	case wxID_SHOW_STATUS: {
		bool newstate = menu_ischecked(wxID_SHOW_STATUS);
		if(newstate && !spanel_shown)
			toplevel->Add(spanel);
		else if(!newstate && spanel_shown)
			toplevel->Detach(spanel);
		spanel_shown = newstate;
		toplevel->Layout();
		toplevel->SetSizeHints(this);
		Fit();
		return;
	}
	case wxID_SET_SPEED: {
		std::string value;
		bool bad = false;
		runemufn([&value]() { value = setting::is_set("targetfps") ? setting::get("targetfps") : ""; });
		value = pick_text(this, "Set speed", "Enter percentage speed (or \"infinite\"):", value);
		runemufn([&bad, &value]() { try { setting::set("targetfps", value); } catch(...) { bad = true; } });
		if(bad)
			wxMessageBox(wxT("Invalid speed"), _T("Error"), wxICON_EXCLAMATION | wxOK, this);
	}
	case wxID_SET_VOLUME: {
		std::string value;
		regex_results r;
		double parsed = 1;
		value = pick_text(this, "Set volume", "Enter volume in absolute units, percentage (%) or dB:",
			last_volume);
		if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)", value))
			parsed = strtod(r[1].c_str(), NULL);
		else if(r = regex("([0-9]*\\.[0-9]+|[0-9]+)%", value))
			parsed = strtod(r[1].c_str(), NULL) / 100;
		else if(r = regex("([+-]?([0-9]*.[0-9]+|[0-9]+))dB", value))
			parsed = pow(10, strtod(r[1].c_str(), NULL) / 20);
		else {
			wxMessageBox(wxT("Invalid volume"), _T("Error"), wxICON_EXCLAMATION | wxOK, this);
			return;
		}
		last_volume = value;
		runemufn([parsed]() { platform::global_volume = parsed; });
	}
	};
}
