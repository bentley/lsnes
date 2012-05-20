#include "core/bsnes.hpp"
#include <gameboy/gameboy.hpp>
#include "interface/core.hpp"
#include "library/minmax.hpp"
#include "library/sha256.hpp"
#include <sstream>
#include <stdexcept>

/**
 * Number clocks per field/frame on NTSC/PAL
 */
#define DURATION_NTSC_FRAME 357366
#define DURATION_NTSC_FIELD 357368
#define DURATION_PAL_FRAME 425568
#define DURATION_PAL_FIELD 425568

namespace
{
	uint64_t ntsc_magic[4] = {178683, 10738636, 16639264, 596096};
	uint64_t pal_magic[4] = {6448, 322445, 19997208, 266440};

	class my_interfaced : public SNES::Interface
	{
		string path(SNES::Cartridge::Slot slot, const string &hint)
		{
			return "./";
		}
	};

	struct bsnes_region_info_structure : public region_info_structure
	{
		bsnes_region_info_structure(const std::string& _iname, const std::string& _hname, bool _autod)
		{
			hname = _hname;
			iname = _iname;
			autod = _autod;
		}
		~bsnes_region_info_structure() {}
		std::string get_hname() { return hname; }
		std::string get_iname() { return iname; }
		bool compatible(const std::string& movie) { return autod || movie == iname; }
		std::string hname;
		std::string iname;
		bool autod;
	};

	struct bsnes_sysregion_info_structure : public sysregion_info_structure
	{
		bsnes_sysregion_info_structure(const std::string& _iname, bool _pal)
		{
			iname = _iname;
			pal = _pal;
		}
		~bsnes_sysregion_info_structure() {}
		std::string get_iname() { return iname; }
		void get_length_magic(uint64_t* magic)
		{
			if(pal)
				memcpy(magic, pal_magic, sizeof(pal_magic));
			else
				memcpy(magic, ntsc_magic, sizeof(ntsc_magic));
		}
		std::string iname;
		bool pal;
	};

	struct bsnes_rom_info_structure : public rom_info_structure
	{
		bsnes_rom_info_structure(const std::string& _iname, const std::string& _hname, unsigned _mflags)
		{
			iname = _iname;
			hname = _hname;
			mflags = _mflags;
		}
		~bsnes_rom_info_structure() {}
		std::string get_iname() { return iname; }
		std::string get_hname() { return hname; }
		bool has_markup() { return true; }
		size_t headersize(size_t imagesize)
		{
			return (imagesize % 1024 == 512) ? 512 : 0;
		}
		virtual unsigned mandatory_flags() { return mflags; }
		std::string iname;
		std::string hname;
		unsigned mflags;
	};

	bsnes_region_info_structure r_autodetect("autodetect", "Autodetect", true);
	bsnes_region_info_structure r_ntsc("ntsc", "NTSC", false);
	bsnes_region_info_structure r_pal("pal", "PAL", false);
	bsnes_sysregion_info_structure t_snes_ntsc("snes_ntsc", false);
	bsnes_sysregion_info_structure t_snes_pal("snes_pal", true);
	bsnes_sysregion_info_structure t_bsx("bsx", false);
	bsnes_sysregion_info_structure t_bsxslotted("bsxslotted", false);
	bsnes_sysregion_info_structure t_sufamiturbo("sufamiturbo", false);
	bsnes_sysregion_info_structure t_sgb_ntsc("sgb_ntsc", false);
	bsnes_sysregion_info_structure t_sgb_pal("sgb_pal", true);
	bsnes_rom_info_structure g_snes("rom", "Cartridge ROM", 1);
	bsnes_rom_info_structure g_bsx_bios("bsxbios", "BS-X BIOS", 1);
	bsnes_rom_info_structure g_bsx_flash("bsxflash", "BS-X FLASH", 2);
	bsnes_rom_info_structure g_st_bios("stbios", "Sufami Turbo BIOS", 1);
	bsnes_rom_info_structure g_st_slota("stslota", "Slot A ROM", 2);
	bsnes_rom_info_structure g_st_slotb("stslotb", "Slot B ROM", 2);
	bsnes_rom_info_structure g_sgb_bios("sgbbios", "SGB BIOS", 1);
	bsnes_rom_info_structure g_dmg("dmg", "DMG ROM", 2);

	struct bsnes_systype : public systype_info_structure
	{
		bsnes_systype(int index)
		{
			switch(index) {
			case 0:
				iname = "snes";
				hname = "SNES";
				regslots.push_back(&r_autodetect);
				regslots.push_back(&r_ntsc);
				regslots.push_back(&r_pal);
				romslots.push_back(&g_snes);
				sysregions["ntsc"] = &t_snes_ntsc;
				sysregions["pal"] = &t_snes_pal;
				break;
			case 1:
				iname = "bsx";
				hname = "BS-X (non-slotted)";
				regslots.push_back(&r_ntsc);
				romslots.push_back(&g_bsx_bios);
				romslots.push_back(&g_bsx_flash);
				sysregions["ntsc"] = &t_bsx;
				break;
			case 2:
				iname = "bsxslotted";
				hname = "BS-X (slotted)";
				regslots.push_back(&r_ntsc);
				romslots.push_back(&g_bsx_bios);
				romslots.push_back(&g_bsx_flash);
				sysregions["ntsc"] = &t_bsxslotted;
				break;
			case 3:
				iname = "sufamiturbo";
				hname = "Sufami Turbo";
				regslots.push_back(&r_ntsc);
				romslots.push_back(&g_st_bios);
				romslots.push_back(&g_st_slota);
				romslots.push_back(&g_st_slotb);
				sysregions["ntsc"] = &t_sufamiturbo;
				break;
			case 4:
				iname = "sgb";
				hname = "Super Game Boy";
				regslots.push_back(&r_autodetect);
				regslots.push_back(&r_ntsc);
				regslots.push_back(&r_pal);
				romslots.push_back(&g_sgb_bios);
				romslots.push_back(&g_dmg);
				sysregions["ntsc"] = &t_sgb_ntsc;
				sysregions["pal"] = &t_sgb_pal;
				break;
			};
		}
		~bsnes_systype() {};
		std::string get_iname() { return iname; }
		std::string get_hname() { return hname; }
		size_t region_slots() { return regslots.size(); };
		struct region_info_structure* region_slot(size_t index)
		{
			return (index < regslots.size()) ? regslots[index] : NULL;
		}
		size_t rom_slots() { return romslots.size(); }
		struct rom_info_structure* rom_slot(size_t index)
		{
			return (index < romslots.size()) ? romslots[index] : NULL;
		}
		struct sysregion_info_structure* get_sysregion(const std::string& region)
		{
			return sysregions.count(region) ? sysregions[region] : NULL;
		}
		std::string iname;
		std::string hname;
		std::vector<region_info_structure*> regslots;
		std::vector<rom_info_structure*> romslots;
		std::map<std::string, sysregion_info_structure*> sysregions;
	};

	bsnes_systype s_snes(0);
	bsnes_systype s_bsx(1);
	bsnes_systype s_bsxslotted(2);
	bsnes_systype s_sufamiturbo(3);
	bsnes_systype s_sgb(4);

	void internal_load(const std::vector<std::vector<char>>& romslots,
		const std::vector<std::vector<char>>&markslots, size_t i, const uint8_t*& rom, const char*& xml,
		size_t& romsize)
	{
		if(i < romslots.size() && romslots[i].size()) {
			rom = reinterpret_cast<const uint8_t*>(&romslots[i][0]);
			romsize = romslots[i].size();
		}
		if(i < markslots.size() && markslots[i].size())
			xml = &markslots[i][0];
	}
}

std::string emucore_get_version()
{
	std::ostringstream x;
	x << snes_library_id() << " (" << SNES::Info::Profile << " core)";
	return x.str();
}

std::pair<uint32_t, uint32_t> emucore_get_video_rate(bool interlace)
{
	uint32_t d;
	if(SNES::system.region() == SNES::System::Region::PAL)
		d = interlace ? DURATION_PAL_FIELD : DURATION_PAL_FRAME;
	else
		d = interlace ? DURATION_NTSC_FIELD : DURATION_NTSC_FRAME;
	return std::make_pair(SNES::system.cpu_frequency(), d);
}

std::pair<uint32_t, uint32_t> emucore_get_audio_rate()
{
	return std::make_pair(SNES::system.apu_frequency(), 768);
}

void emucore_basic_init()
{
	static bool done = false;
	if(done)
		return;
	my_interfaced* intrf = new my_interfaced;
	SNES::interface = intrf;
	done = true;
}

namespace
{
	//Wunderbar... bsnes v087 goes to change the sram names...
	std::string remap_sram_name(const std::string& name, SNES::Cartridge::Slot slotname)
	{
		std::string iname = name;
		if(iname == "bsx.ram")
			iname = ".bss";
		if(iname == "bsx.psram")
			iname = ".bsp";
		if(iname == "program.rtc")
			iname = ".rtc";
		if(iname == "upd96050.ram")
			iname = ".dsp";
		if(iname == "program.ram")
			iname = ".srm";
		if(slotname == SNES::Cartridge::Slot::SufamiTurboA)
			return "slota" + iname;
		if(slotname == SNES::Cartridge::Slot::SufamiTurboB)
			return "slotb" + iname;
		else
			return iname.substr(1);
	}

	struct bsnes_sram_slot : public sram_slot_structure
	{
		std::string name;
		size_t size;
		unsigned char* memory;

		bsnes_sram_slot(const nall::string& _id, SNES::Cartridge::Slot slotname, unsigned char* mem,
			size_t sramsize)
		{
			std::string id(_id, _id.length());
			name = remap_sram_name(id, slotname);
			memory = mem;
			size = sramsize;
		}

		std::string get_name()
		{
			return name;
		}

		void copy_to_core(const std::vector<char>& content)
		{
			memcpy(memory, &content[0], (content.size() < size) ? content.size() : size);
		}

		void copy_from_core(std::vector<char>& content)
		{
			content.resize(size);
			memcpy(&content[0], memory, size);
		}

		size_t get_size()
		{
			return size;
		}
	};

	struct bsnes_vma_slot : public vma_structure
	{
		bsnes_vma_slot(const std::string& _name, unsigned char* _memory, uint64_t _base, uint64_t _size,
			endian _rendian, bool _readonly)
			: vma_structure(_name, _base, _size, _rendian, _readonly)
		{
			memory = _memory;
		}

		void copy_from_core(uint64_t start, char* buffer, uint64_t _size)
		{
			uint64_t inrange = _size;
			if(start >= size)
				inrange = 0;
			inrange = min(inrange, size - start);
			if(inrange)
				memcpy(buffer, memory + start, inrange);
			if(inrange < _size)
				memset(buffer + inrange, 0, _size - inrange);
		}

		void copy_to_core(uint64_t start, const char* buffer, uint64_t _size)
		{
			uint64_t inrange = _size;
			if(start >= size)
				inrange = 0;
			inrange = min(inrange, size - start);
			if(inrange && !readonly)
				memcpy(memory + start, buffer, inrange);
		}
	private:
		unsigned char* memory;
	};

	std::vector<bsnes_sram_slot*> sram_slots;
	std::vector<bsnes_vma_slot*> vma_slots;
}

size_t emucore_sram_slots()
{
	return sram_slots.size();
}

struct sram_slot_structure* emucore_sram_slot(size_t index)
{
	if(index >= sram_slots.size())
		return NULL;
	return sram_slots[index];
}

size_t emucore_vma_slots()
{
	return vma_slots.size();
}

struct vma_structure* emucore_vma_slot(size_t index)
{
	if(index >= vma_slots.size())
		return NULL;
	return vma_slots[index];
}

void emucore_refresh_cart()
{
	std::vector<bsnes_sram_slot*> new_sram_slots;
	std::vector<bsnes_vma_slot*> new_vma_slots;
	size_t slots = SNES::cartridge.nvram.size();
	new_sram_slots.resize(slots);
	for(size_t i = 0; i < slots; i++)
		new_sram_slots[i] = NULL;

	try {
		for(unsigned i = 0; i < slots; i++) {
			SNES::Cartridge::NonVolatileRAM& s = SNES::cartridge.nvram[i];
			new_sram_slots[i] = new bsnes_sram_slot(s.id, s.slot, s.data, s.size);
		}
		new_vma_slots.push_back(new bsnes_vma_slot("WRAM", SNES::cpu.wram, 0x007E0000, 131072,
			vma_structure::E_LITTLE, false));
		new_vma_slots.push_back(new bsnes_vma_slot("APURAM", SNES::smp.apuram, 0x00000000, 65536,
			vma_structure::E_LITTLE, false));
		new_vma_slots.push_back(new bsnes_vma_slot("VRAM", SNES::ppu.vram, 0x00010000, 65536,
			vma_structure::E_LITTLE, false));
		new_vma_slots.push_back(new bsnes_vma_slot("OAM", SNES::ppu.oam, 0x00020000, 544,
			vma_structure::E_LITTLE, false));
		new_vma_slots.push_back(new bsnes_vma_slot("CGRAM", SNES::ppu.cgram, 0x00021000, 512,
			vma_structure::E_LITTLE, false));
		if(SNES::cartridge.ram.size() > 0)
			new_vma_slots.push_back(new bsnes_vma_slot("SRAM", SNES::cartridge.ram.data(), 0x10000000,
				SNES::cartridge.ram.size(), vma_structure::E_LITTLE, false));
		new_vma_slots.push_back(new bsnes_vma_slot("ROM", SNES::cartridge.rom.data(), 0x80000000,
			SNES::cartridge.rom.size(), vma_structure::E_LITTLE, true));
		if(SNES::cartridge.has_srtc())
			new_vma_slots.push_back(new bsnes_vma_slot("RTC", SNES::srtc.rtc, 0x00022000, 20,
				vma_structure::E_LITTLE, false));
		if(SNES::cartridge.has_spc7110rtc())
			new_vma_slots.push_back(new bsnes_vma_slot("RTC", SNES::spc7110.rtc, 0x00022000, 20,
				vma_structure::E_LITTLE, false));
		if(SNES::cartridge.has_necdsp()) {
			new_vma_slots.push_back(new bsnes_vma_slot("DSPRAM",
				reinterpret_cast<uint8_t*>(SNES::necdsp.dataRAM), 0x00023000, 4096,
				vma_structure::E_HOST, false));
			new_vma_slots.push_back(new bsnes_vma_slot("DSPPROM",
				reinterpret_cast<uint8_t*>(SNES::necdsp.programROM), 0xF0000000, 65536,
				vma_structure::E_HOST, true));
			new_vma_slots.push_back(new bsnes_vma_slot("DSPDROMM",
				reinterpret_cast<uint8_t*>(SNES::necdsp.dataROM), 0xF0010000, 4096,
				vma_structure::E_HOST, true));
		}
		if(SNES::cartridge.mode() == SNES::Cartridge::Mode::Bsx ||
			SNES::cartridge.mode() == SNES::Cartridge::Mode::BsxSlotted) {
			new_vma_slots.push_back(new bsnes_vma_slot("BSXFLASH",
				SNES::bsxflash.memory.data(), 0x90000000, SNES::bsxflash.memory.size(),
				vma_structure::E_LITTLE, true));
			new_vma_slots.push_back(new bsnes_vma_slot("BSX_RAM",
				SNES::bsxcartridge.sram.data(), 0x20000000, SNES::bsxcartridge.sram.size(),
				vma_structure::E_LITTLE, false));
			new_vma_slots.push_back(new bsnes_vma_slot("BSX_PRAM",
				SNES::bsxcartridge.psram.data(), 0x30000000, SNES::bsxcartridge.psram.size(),
				vma_structure::E_LITTLE, false));
		}
		if(SNES::cartridge.mode() == SNES::Cartridge::Mode::SufamiTurbo) {
			new_vma_slots.push_back(new bsnes_vma_slot("SLOTA_ROM", SNES::sufamiturbo.slotA.rom.data(),
				0x90000000, SNES::sufamiturbo.slotA.rom.size(), vma_structure::E_LITTLE, true));
			new_vma_slots.push_back(new bsnes_vma_slot("SLOTB_ROM", SNES::sufamiturbo.slotB.rom.data(),
				0xA0000000, SNES::sufamiturbo.slotB.rom.size(), vma_structure::E_LITTLE, true));
			new_vma_slots.push_back(new bsnes_vma_slot("SLOTA_RAM", SNES::sufamiturbo.slotA.ram.data(),
				0x20000000, SNES::sufamiturbo.slotA.ram.size(), vma_structure::E_LITTLE, false));
			new_vma_slots.push_back(new bsnes_vma_slot("SLOTB_RAM", SNES::sufamiturbo.slotB.ram.data(),
				0x30000000, SNES::sufamiturbo.slotB.ram.size(), vma_structure::E_LITTLE, false));
		}
		if(SNES::cartridge.mode() == SNES::Cartridge::Mode::SuperGameBoy) {
			new_vma_slots.push_back(new bsnes_vma_slot("GBROM", GameBoy::cartridge.romdata,
				0x90000000, GameBoy::cartridge.romsize, vma_structure::E_LITTLE, true));
			new_vma_slots.push_back(new bsnes_vma_slot("GBRAM", GameBoy::cartridge.ramdata,
				0x20000000, GameBoy::cartridge.ramsize, vma_structure::E_LITTLE, false));
		}
	} catch(...) {
		for(auto i : new_sram_slots)
			delete i;
		for(auto i : new_vma_slots)
			delete i;
		throw;
	}

	std::swap(sram_slots, new_sram_slots);
	std::swap(vma_slots, new_vma_slots);
	for(auto i : new_sram_slots)
		delete i;
	for(auto i : new_vma_slots)
		delete i;
}

std::vector<char> emucore_serialize(bool nochecksum)
{
	std::vector<char> ret;
	serializer s = SNES::system.serialize();
	ret.resize(s.size());
	memcpy(&ret[0], s.data(), s.size());
	if(nochecksum)
		return ret;
	size_t offset = ret.size();
	unsigned char tmp[32];
	sha256::hash(tmp, ret);
	ret.resize(offset + 32);
	memcpy(&ret[offset], tmp, 32);
	return ret;
}

void emucore_unserialize(const std::vector<char>& buf, bool nochecksum)
{
	if(nochecksum) {
		serializer s(reinterpret_cast<const uint8_t*>(&buf[0]), buf.size());
		if(!SNES::system.unserialize(s))
			throw std::runtime_error("SNES core rejected savestate");
		return;
	}
	if(buf.size() < 32)
		throw std::runtime_error("Savestate corrupt");
	unsigned char tmp[32];
	sha256::hash(tmp, reinterpret_cast<const uint8_t*>(&buf[0]), buf.size() - 32);
	if(memcmp(tmp, &buf[buf.size() - 32], 32))
		throw std::runtime_error("Savestate corrupt");
	serializer s(reinterpret_cast<const uint8_t*>(&buf[0]), buf.size() - 32);
	if(!SNES::system.unserialize(s))
		throw std::runtime_error("SNES core rejected savestate");
}

size_t emucore_systype_slots()
{
	return 5;
}

struct systype_info_structure* emucore_systype_slot(size_t index)
{
	switch(index) {
	case 0:		return &s_snes;
	case 1:		return &s_bsx;
	case 2:		return &s_bsxslotted;
	case 3:		return &s_sufamiturbo;
	case 4:		return &s_sgb;
	default:	return NULL;
	};
}

void emucore_load_rom(systype_info_structure* rtype, region_info_structure* region,
	const std::vector<std::vector<char>>& romslots, const std::vector<std::vector<char>>& markslots)
{
	if(region == &r_autodetect)
		SNES::config.region = SNES::System::Region::Autodetect;
	else if(region == &r_ntsc)
		SNES::config.region = SNES::System::Region::NTSC;
	else if(region == &r_pal)
		SNES::config.region = SNES::System::Region::PAL;
	else
		throw std::runtime_error("Trying to force unknown region");
	const uint8_t* rom0 = NULL;
	const char* xml0 = NULL;
	size_t rom0size = 0;
	const uint8_t* rom1 = NULL;
	const char* xml1 = NULL;
	size_t rom1size = 0;
	const uint8_t* rom2 = NULL;
	const char* xml2 = NULL;
	size_t rom2size = 0;
	internal_load(romslots, markslots, 0, rom0, xml0, rom0size);
	internal_load(romslots, markslots, 1, rom1, xml1, rom1size);
	internal_load(romslots, markslots, 2, rom2, xml2, rom2size);

	if(rtype == &s_snes) {
		if(!snes_load_cartridge_normal(xml0, rom0, rom0size))
			throw std::runtime_error("Can't load cartridge ROM");
	} else if(rtype == &s_bsx) {
		if(!snes_load_cartridge_bsx(xml0, rom0, rom0size, xml1, rom1, rom1size))
			throw std::runtime_error("Can't load cartridge ROM");
	} else if(rtype == &s_bsxslotted) {
		if(!snes_load_cartridge_bsx_slotted(xml0, rom0, rom0size, xml1, rom1, rom1size))
			throw std::runtime_error("Can't load cartridge ROM");
	} else if(rtype == &s_sgb) {
		if(!snes_load_cartridge_super_game_boy(xml0, rom0, rom0size, xml1, rom1, rom1size))
			throw std::runtime_error("Can't load cartridge ROM");
	} else if(rtype == &s_sufamiturbo) {
		if(!snes_load_cartridge_sufami_turbo(xml0, rom0, rom0size, xml1, rom1, rom1size, xml2, rom2, rom2size))
			throw std::runtime_error("Can't load cartridge ROM");
	} else
		throw std::runtime_error("Unknown cartridge type");
	snes_power();
	emucore_refresh_cart();
}

struct region_info_structure* emucore_current_region()
{
	return snes_get_region() ? &r_pal : &r_ntsc;
}

void emucore_pre_load_settings()
{
	SNES::config.random = false;
	SNES::config.expansion_port = SNES::System::ExpansionPortDevice::None;
}