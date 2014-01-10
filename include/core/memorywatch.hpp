#ifndef _memorywatch__hpp__included__
#define _memorywatch__hpp__included__

#include <string>
#include <stdexcept>
#include <set>
#include "library/memorywatch.hpp"
#include "library/json.hpp"

std::set<std::string> get_watches() throw(std::bad_alloc);
std::string get_watchexpr_for(const std::string& w) throw(std::bad_alloc);
void set_watchexpr_for(const std::string& w, const std::string& expr) throw(std::bad_alloc);

/**
 * lsnes memory watch printer variables.
 */
struct lsnes_memorywatch_printer
{
/**
 * Ctor.
 */
	lsnes_memorywatch_printer();
/**
 * Serialize the printer to JSON value.
 */
	JSON::node serialize();
/**
 * Unserialize the printer from JSON value.
 */
	void unserialize(const JSON::node& node);
/**
 * Get a printer object corresponding to this object.
 */
	gcroot_pointer<memorywatch_item_printer> get_printer_obj(
		std::function<gcroot_pointer<mathexpr>(const std::string& n)> vars);
	//Fields.
	enum position_category {
		PC_DISABLED,
		PC_MEMORYWATCH,
		PC_ONSCREEN
	} position;
	bool cond_enable;			//Ignored for disabled.
	std::string enabled;			//Ignored for disabled.
	std::string onscreen_xpos;
	std::string onscreen_ypos;
	bool onscreen_alt_origin_x;
	bool onscreen_alt_origin_y;
	bool onscreen_cliprange_x;
	bool onscreen_cliprange_y;
	std::string onscreen_font;		//"" is system default.
	int64_t onscreen_fg_color;
	int64_t onscreen_bg_color;
	int64_t onscreen_halo_color;
};

/**
 * lsnes memory watch item.
 */
struct lsnes_memorywatch_item
{
/**
 * Ctor.
 */
	lsnes_memorywatch_item();
/**
 * Serialize the item to JSON value.
 */
	JSON::node serialize();
/**
 * Unserialize the item from JSON value.
 */
	void unserialize(const JSON::node& node);
/**
 * Get memory read operator.
 *
 * If bytes == 0, returns NULL.
 */
	memorywatch_memread_oper* get_memread_oper();
	//Fields
	lsnes_memorywatch_printer printer;	//The printer.
	std::string expr;			//The main expression.
	std::string format;			//Format.
	unsigned bytes;				//Number of bytes to read (0 => Not memory read operator).
	bool signed_flag;			//Is signed?
	bool float_flag;			//Is float?
	int endianess;				//Endianess (-1 => little, 0 => host, 1 => Big).
	uint64_t scale_div;			//Scale divisor.
	uint64_t addr_base;			//Address base.
	uint64_t addr_size;			//Address size (0 => All).
	memory_space* mspace;			//Memory space to read.
};

struct lsnes_memorywatch_set
{
/**
 * Get the specified memory watch item.
 */
	lsnes_memorywatch_item& get(const std::string& name);
/**
 * Get the specified memory watch item as JSON serialization.
 *
 * Parameter name: The item name.
 * Parameter printer: JSON pretty-printer to use.
 */
	std::string get_string(const std::string& name, JSON::printer* printer = NULL);
/**
 * Set the specified memory watch item. Fills the runtime variables.
 *
 * Parameter name: The name of the new item.
 * Parameter item: The item to insert. Fields are shallow-copied.
 */
	void set(const std::string& name, lsnes_memorywatch_item& item);
/**
 * Set the specified memory watch item from JSON serialization. Fills the runtime variables.
 *
 * Parameter name: The name of the new item.
 * Parameter item: The serialization of item to insert.
 */
	void set(const std::string& name, const std::string& item);
/**
 * Set multiple items at once.
 *
 * Parameter list: The list of items.
 */
	void set_multi(std::list<std::pair<std::string, lsnes_memorywatch_item>>& list);
/**
 * Set multiple items at once from JSON descriptions.
 *
 * Parameter list: The list of items.
 */
	void set_multi(std::list<std::pair<std::string, std::string>>& list);
/**
 * Rename a memory watch item.
 *
 * Parameter oname: The old name.
 * Parameter newname: The new name.
 * Returns: True on success, false if failed (new item already exists).
 */
	bool rename(const std::string& oname, const std::string& newname);
/**
 * Delete an item.
 *
 * Parameter name: The name of the item to delete.
 */
	void clear(const std::string& name);
/**
 * Delete multiple items.
 *
 * Parameter names: The names of the items to delete.
 */
	void clear_multi(const std::set<std::string>& name);
/**
 * Enumerate item names.
 */
	std::set<std::string> enumerate();
/**
 * Get value of specified memory watch as a string.
 */
	std::string get_value(const std::string& name);
/**
 * Watch all the items.
 *
 * Parameter rq: The render queue to use.
 */
	void watch(struct framebuffer::queue& rq);
private:
	void rebuild(std::map<std::string, lsnes_memorywatch_item>& nitems);
	std::map<std::string, lsnes_memorywatch_item> items;
	memorywatch_set watch_set;
};

extern lsnes_memorywatch_set lsnes_memorywatch;

#endif
