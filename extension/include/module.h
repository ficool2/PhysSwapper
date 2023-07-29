// Unused instruction byte in x86 architecture
#define SIGNATURE_WILDCARD 0x2A
#define SIGNATURE(sig) #sig, sizeof(#sig) - 1

enum ModuleName
{
	CLIENT,
	SERVER,
	ENGINE,
	DATACACHE,
	VPHYSICS,

	MODULE_LAST
};

#ifndef _WIN32
struct ModuleIterate_t
{
	link_map* Map;
	uintptr_t* OutSize;
};

inline int ModuleIterate(struct dl_phdr_info* info, size_t, void* data)
{
	ModuleIterate_t* Iterate = (ModuleIterate_t*)data;
	if (info->dlpi_addr == Iterate->Map->l_addr)
	{
		for (int i = 0; i < info->dlpi_phnum; i++)
		{
			if (info->dlpi_phdr[i].p_type == PT_LOAD)
			{
				size_t end = info->dlpi_phdr[i].p_vaddr + info->dlpi_phdr[i].p_memsz;
				if (end > *Iterate->OutSize)
					*Iterate->OutSize = end;
			}
		}
		return 1;
	}
	return 0;
}
#endif

struct Module_t
{
	Module_t(const char* _name)
	{
		name = _name;
		handle = nullptr;
		base = 0;
		size = 0;
	}

	void Init(CSysModule* modulehandle)
	{
		handle = modulehandle;

#ifdef _WIN32
		MODULEINFO info = { 0 };
		GetModuleInformation(GetCurrentProcess(), (HMODULE)modulehandle, &info, sizeof(info));
		base = (uintptr_t)info.lpBaseOfDll;
		size = (uintptr_t)info.SizeOfImage;
#else
		ModuleIterate_t Iterate;
		Iterate.Map = (link_map*)modulehandle;
		Iterate.OutSize = &size;

		base = Iterate.Map->l_addr;
		dl_iterate_phdr(ModuleIterate, &Iterate);
#endif
	}

	bool IsValidAddress(uintptr_t addr)
	{
		return addr >= base && addr < base + size;
	}

	const char* name;
	CSysModule* handle;
	uintptr_t base;
	uintptr_t size;
};

Module_t g_Modules[MODULE_LAST] =
{
	"client",
	"server",
	"engine",
	"datacache",
	"vphysics"
};

const Module_t* g_ModuleClient = &g_Modules[CLIENT];

inline Module_t* GetModule(const char* Name)
{
	for (Module_t& Module : g_Modules)
	{
		if (!strcmp(Module.name, Name))
		{
			return &Module;
			break;
		}
	}

	return nullptr;
}

inline ModuleName GetModuleByName(const char* Name)
{
	for (int i = 0; i < MODULE_LAST; i++)
	{
		Module_t& Module = g_Modules[i];
		if (!strcmp(Module.name, Name))
			return (ModuleName)i;
	}

	return MODULE_LAST;
}
