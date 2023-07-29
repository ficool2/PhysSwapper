

template<typename T>
inline void* MemberFuncPtrToVoidPtr(T fn)
{
	return reinterpret_cast<void*&>(fn);
}

template <typename T>
inline T PatchMemory(void* Address, T Value)
{
	T PrevMemory = *(T*)Address;
#ifdef _WIN32
	unsigned long PrevProtect, Dummy;
	VirtualProtect(Address, sizeof(T), PAGE_EXECUTE_READWRITE, &PrevProtect);
	*(T*)Address = Value;
	VirtualProtect(Address, sizeof(T), PrevProtect, &Dummy);
#else
	size_t PageSize = sysconf(_SC_PAGESIZE);
	uintptr_t Start = (uintptr_t)Address;
	uintptr_t End = Start + sizeof(T);
	uintptr_t PageStart = Start & -PageSize;
	mprotect((void*)PageStart, End - PageStart, PROT_READ|PROT_WRITE);
	*(T*)Address = Value;
	mprotect((void*)PageStart, End - PageStart, PROT_READ|PROT_EXEC);
#endif
	return PrevMemory;
}

inline uintptr_t FindSignature(Module_t* Module, const char* Sig, size_t Length)
{
	uintptr_t Start = Module->base, End = Start + Module->size - Length;
	for (uintptr_t i = Start; i < End; i++)
	{
		bool bFound = true;
		for (uintptr_t j = 0; j < Length; j++)
			bFound &= Sig[j] == SIGNATURE_WILDCARD || Sig[j] == *(char*)(i + j);
		if (bFound)
			return i;
	}

	return 0;
}

struct BaseRef;
std::vector<BaseRef*> g_BaseRefs;

struct BaseRef
{
	BaseRef(const char* Name, ModuleName Module)
	{
		m_Name = Name;
		m_Module = Module;
		m_Ref = nullptr;
		g_BaseRefs.push_back(this);
	}

	virtual void Init(uintptr_t Addr) = 0;
	virtual bool IsFunction() = 0;

	const char* m_Name;
	ModuleName m_Module;
	void* m_Ref;
};

template <typename T>
struct DataRef : BaseRef
{
	DataRef(const char* Name, ModuleName Module) : BaseRef(Name, Module) {}

	virtual void Init(uintptr_t Addr) override
	{
		m_Ref = *(T**)Addr;
	}

	virtual bool IsFunction() override
	{
		return false;
	}

	bool Init(const char* Sig, size_t Length, uintptr_t Offset)
	{
		Module_t* Module = &g_Modules[m_Module];
		uintptr_t Addr = FindSignature(Module, Sig, Length);
		if (Addr == 0)
		{
			MsgColor(ColorRed, "Error: Failed to find signature for (%s) %s", Module->name, m_Name);
			return false;
		}
		Addr += Offset;
		if (!Module->IsValidAddress(Addr))
		{
			MsgColor(ColorRed, "Error: Out of bounds address for (%s) %s at %p", Module->name, m_Name, Addr);
			return false;
		}
		Init(Addr);
		return true;
	}

	explicit operator bool() const { return m_Ref != nullptr; }
	T* operator->() { return m_Ref; }
	void operator=(const T& obj)
	{
		*(T*)m_Ref = obj;
	}
};

struct FuncRef : BaseRef
{
	FuncRef(const char* Name, ModuleName Module) : BaseRef(Name, Module) {}

	virtual void Init(uintptr_t Addr) override
	{
		m_Ref = (void*)Addr;
	}

	virtual bool IsFunction() override
	{
		return true;
	}

	bool Init(const char* Sig, size_t Length)
	{
		Module_t* Module = &g_Modules[m_Module];
		uintptr_t Addr = FindSignature(Module, Sig, Length);
		if (Addr == 0)
		{
			MsgColor(ColorRed, "Error: Failed to find function signature for (%s) %s", Module->name, m_Name);
			return false;
		}
		if (!Module->IsValidAddress(Addr))
		{
			MsgColor(ColorRed, "Error: Out of bounds function address for (%s) %s at %p", Module->name, m_Name, Addr);
			return false;
		}
		Init(Addr);
		return true;
	}

	explicit operator bool() const { return m_Ref != nullptr; }
};

template< typename Ret, typename... Args >
struct FuncRefCdecl : public FuncRef
{
public:
	using FuncRef::FuncRef;
	using FunctionType = Ret(__cdecl*)(Args...);
	Ret operator()(Args... args) { return ((FunctionType)m_Ref)(args...); }
};

template< typename Ret, typename... Args >
class FuncStdcall : public FuncRef
{
public:
	using FuncRef::FuncRef;
	using FunctionType = Ret(__stdcall*)(Args...);
	Ret operator()(Args... args) { return ((FunctionType)m_Ref)(args...); }
};

#ifdef _WIN32
template< typename Ret, typename Obj, typename... Args >
struct FuncRefThiscall : FuncRef
{
	using FuncRef::FuncRef;
	using FunctionType = Ret(__fastcall*)(Obj*, void*, Args...);
	Ret operator()(Obj* This, Args... args) { return ((FunctionType)m_Ref)(This, nullptr, args...); }
};
#else
template< typename Ret, typename Obj, typename... Args >
struct FuncRefThiscall : FuncRef
{
	using FuncRef::FuncRef;
	using FunctionType = Ret(__cdecl*)(Obj*, Args...);
	Ret operator()(Obj* This, Args... args) { return ((FunctionType)m_Ref)(This, args...); }
};
#endif

inline BaseRef* FindBaseRef(const char* Name, ModuleName Module, bool Function)
{
	for (BaseRef* BaseRef : g_BaseRefs)
	{
		if (Function && !BaseRef->IsFunction())
			continue;
		else if (!Function && BaseRef->IsFunction())
			continue;
		if (BaseRef->m_Module != Module)
			continue;
		if (!strcmp(BaseRef->m_Name, Name))
		{
			return BaseRef;
			break;
		}
	}
	return nullptr;
}

#pragma pack( push, 1 ) 
struct AsmJump_t
{
	AsmJump_t() {};
	AsmJump_t(byte _Opcode, int32 _Offset)
	{
		Opcode = _Opcode;
		Offset = _Offset;
	}

	union
	{
		struct
		{
			byte Opcode;
			int32 Offset;
		};
		union
		{
			byte SavedBytes[5];
		};
	};
};
#pragma pack( pop )

inline int32 CalcRelativeJmp(void* Start, void* Target)
{
	return (int32)Target - (int32)Start - 5;
}