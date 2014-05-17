#include "core/instance.hpp"
#include "core/keymapper.hpp"
#include "core/command.hpp"
#include "lua/internal.hpp"
#include <stdexcept>

namespace
{
	int kbd_bind(lua::state& L, lua::parameters& P)
	{
		std::string mod, mask, key, cmd;

		P(mod, mask, key, cmd);

		CORE().mapper.bind(mod, mask, key, cmd);
		return 0;
	}

	int kbd_unbind(lua::state& L, lua::parameters& P)
	{
		std::string mod, mask, key;

		P(mod, mask, key);

		CORE().mapper.unbind(mod, mask, key);
		return 0;
	}

	int kbd_alias(lua::state& L, lua::parameters& P)
	{
		std::string alias, cmds;

		P(alias, cmds);

		CORE().command.set_alias_for(alias, cmds);
		CORE().abindmanager();
		return 0;
	}

	class lua_keyboard_dummy {};
	lua::functions lua_kbd(lua_func_misc, "keyboard", {
		{"bind", kbd_bind},
		{"unbind", kbd_unbind},
		{"alias", kbd_alias},
	});
}
