const Color ColorRed = { 255, 0, 0, 255 };
const Color ColorGreen = { 0, 255, 0, 255 };
const Color ColorAqua = { 0, 255, 255, 255 };
const Color ColorPurple = { 255, 0, 255, 255 };
const Color ColorYellow = { 255, 255, 0, 255 };

extern ConVar phys_swap_debug;

#define MsgColor( clr, fmt, ... ) ConColorMsg( 0, clr, "[" PLUGIN_NAME "] " fmt "\n", __VA_ARGS__ )
#define DbgColor( clr, fmt, ... ) if (phys_swap_debug.GetBool()) ConColorMsg( 0, clr, "[" PLUGIN_NAME "] DBG: " fmt "\n", __VA_ARGS__ );

struct ScopedFile_t
{
	ScopedFile_t()
	{
		m_FileName = nullptr;
		m_Buffer = nullptr;
		m_Size = 0;
	}
	~ScopedFile_t()
	{
		delete[] m_Buffer;
	}

	bool ReadIntoBuffer(const char* FileName)
	{
		FILE* f = fopen(FileName, "rb");
		if (!f)
			return false;

		fseek(f, 0, SEEK_END);
		m_Size = ftell(f);
		m_Buffer = new char[m_Size + 1];
		fseek(f, 0, SEEK_SET);
		fread(m_Buffer, 1, m_Size, f);
		m_Buffer[m_Size] = '\0';
		fclose(f);
		return true;
	}

	const char* m_FileName;
	char* m_Buffer;
	int m_Size;
};

#ifndef _WIN32
struct FindModule_t
{
	const char* Name;
	const char* Path;
};

static int FindModuleByName(struct dl_phdr_info* info, size_t size, void* data)
{
	FindModule_t* Find = (FindModule_t*)data;
	char* Basename = V_strrchr(info->dlpi_name, CORRECT_PATH_SEPARATOR);
	if (!Basename)
		return 0;
	if (!V_strcmp(Basename + 1, Find->Name))
	{
		Find->Path = info->dlpi_name;
		return 1;
	}
	return 0;
}
#endif

inline CSysModule* GetSysModuleHandle(const char* Name)
{
#ifdef _WIN32
	return (CSysModule*)GetModuleHandle(Name);
#else
	FindModule_t Find = {Name, nullptr};
	if (dl_iterate_phdr(FindModuleByName, &Find) != 0)
	{
		CSysModule* Module = (CSysModule*)dlopen(Find.Path, RTLD_LAZY | RTLD_NOLOAD);
		if (Module != nullptr)
			dlclose(Module);
		return Module;
	}
	return nullptr;
#endif
}

struct ScopedWorkingDir_t
{
	ScopedWorkingDir_t() 
	{
		V_GetCurrentDirectory(WorkingDir, sizeof(WorkingDir));
	};
	~ScopedWorkingDir_t() { Done(); }

	void Set(const char* Dir) 
	{ 
		V_SetCurrentDirectory(Dir);
	}

	void Done() 
	{ 
		if (WorkingDir[0]) 
		{ 
			V_SetCurrentDirectory(WorkingDir); 
			WorkingDir[0] = '\0';
		} 
	}

	char WorkingDir[_MAX_PATH];
};

struct ScopedSysModule_t
{
	ScopedSysModule_t() 
	{ 
		Handle = nullptr; 
	}
	~ScopedSysModule_t() 
	{ 
		if (Handle) Unload();
	}

	CSysModule* Load(const char* Dir)
	{
#ifdef _WIN32
		Handle = (CSysModule*)LoadLibraryEx(Dir, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
		Handle = (CSysModule*)dlopen(Dir, RTLD_NOW);
#endif
		return Handle;
	}

	void Unload()
	{
		if (Handle)
		{
#ifdef _WIN32
			FreeLibrary((HMODULE)Handle);
#else
			dlclose((void*)Handle);
#endif
			Handle = nullptr;
		}
	}

	void Done()
	{
		Handle = nullptr;
	}

	CSysModule* Handle;
};
