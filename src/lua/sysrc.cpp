const char* lua_sysrc_script =
"loopwrapper = function(fn, ...)\n"
"	local routine = coroutine.create(fn);\n"
"	local resume = function(...)\n"
"		if coroutine.status(routine) ~= \"dead\" then\n"
"			local x, y;\n"
"			x, y = coroutine.resume(routine, ...);\n"
"			if not x then\n"
"				error(y);\n"
"			end\n"
"		end\n"
"	end\n"
"	local yield = function()\n"
"		return coroutine.yield(routine);\n"
"	end\n"
"	resume(yield, ...);\n"
"	return resume;\n"
"end;\n"
"print=print2;\n"
"memory2=memory2();\n";