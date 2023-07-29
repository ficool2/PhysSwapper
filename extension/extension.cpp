#define PLUGIN_NAME "PhysSwapper"
#ifdef _WIN32
#define PLATFORM_NAME "windows"
#define DLL_EXT ".dll"
#else
#define PLATFORM_NAME "linux"
#define DLL_EXT ".so"
#endif

#include <iostream>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "Color.h"

#include "extension.h"
#include "ISmmPlugin.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <direct.h> // getcwd
#include <Psapi.h>
#pragma comment(lib, "psapi.lib")
#elif POSIX
#include <link.h>
#include <dlfcn.h>
#include <unistd.h>
#include <sys/mman.h>
#define _getcwd getcwd
#endif

#include "appframework/IAppSystemGroup.h"
#include "tier1/utlvector.h"
#include "tier1/utldict.h"
#include "eiface.h"
#include "vphysics_interface.h"
#include "datacache/imdlcache.h"
#include "filesystem.h"

#include "include/module.h"
#include "include/util.h"
#include "include/memory.h"
#include "include/engine.h"
#include "kv/KeyValue.h"

struct PhysicsEngine_t
{
	const char* VisualName;
	const char* ModuleName;
};

enum PhysicsEngineName
{
	HAVOK,
	JOLT,
	BULLET,

	PHYSICS_NAME_MAX
};

PhysicsEngine_t g_PhysicsEngines[] =
{
	{"Havok", "vphysics"},
	{"Jolt", "vphysics_jolt"},
	{"Bullet", "vphysics_bullet"}
};

PhysicsEngineName g_DefaultPhysicsEngineName = HAVOK;
PhysicsEngineName g_PhysicsEngineName = g_DefaultPhysicsEngineName;
PhysicsEngineName g_PhysicsEngineSwapName = g_DefaultPhysicsEngineName;
PhysicsEngineName g_PhysicsEngineOverrideName = PHYSICS_NAME_MAX;

IPhysics* g_Physics = nullptr;
IPhysicsCollision* g_PhysCollision = nullptr;
IPhysicsSurfaceProps* g_PhysSurfaceProps = nullptr;

IAppSystem* g_PhysicsSystem = nullptr;
IBaseFileSystem* g_FileSystem = nullptr;

// datacache
CMDLCache* g_MDLCache = nullptr;

DataRef<IPhysicsCollision*> g_RefDatacachePhysCollision("physcollision", DATACACHE);
FuncRefThiscall<void, CMDLCache, MDLHandle_t, bool>  CMDLCache__UnserializeVCollide("CMDLCache::UnserializeVCollide", DATACACHE);
FuncRefThiscall<void, CMDLCache, MDLHandle_t>  CMDLCache__DestroyVCollide("CMDLCache::DestroyVCollide", DATACACHE);

// engine
CreateInterfaceFn g_EngineInterface = nullptr;
CEngineAPI* g_EngineAPI = nullptr;
IVEngineServer* g_EngineServer = nullptr;
CDedicatedServerAPI* g_DedicatedAPI = nullptr;

DataRef<IPhysicsSurfaceProps*> g_RefEnginePhysSurfaceProps("physprops", ENGINE);
DataRef<IPhysicsCollision*> g_RefEnginePhysCollision("physcollision", ENGINE);
FuncRefThiscall<bool, CGameServer, const char*, const char*, const char*>  CGameServer__SpawnServer("CGameServer::SpawnServer", ENGINE);

// client
FuncRefCdecl<bool, CreateInterfaceFn> PhysicsDLLInit("PhysicsDLLInit", CLIENT);

// server
DataRef<CreateInterfaceFn> g_RefServerPhysicsInterface("physinterface", SERVER);
FuncRefThiscall<bool, CPhysicsHook> CPhysicsHook__Init("CPhysicsHook::Init", SERVER);

typedef bool (*FuncUTIL_IsCommandIssuedByServerAdmin)();
FuncUTIL_IsCommandIssuedByServerAdmin UTIL_IsCommandIssuedByServerAdmin = nullptr;

CPhysicsHook g_PhysicsHookDummy;

AsmJump_t g_SpawnServerTrampoline;
#ifdef _WIN32
byte g_SpawnServerTrampolinePad = 0x90;
#endif
void* g_SpawnServerPatchAddr = nullptr;

// plugin

char g_GameName[32] = { 0 };
char g_GameFolder[_MAX_PATH] = { 0 };
char g_BinFolder[_MAX_PATH] = { 0 };

std::unordered_map<std::string, uintptr_t> g_Signatures;

bool g_Dedicated = false;

ConVar phys_engine("phys_engine", "", 0, "Current physics engine. Read-only");
ConVar phys_swap_debug("phys_swap_debug", "0", 0, "Enable debug messages when swapping physics");

static void* PhysicsQueryInterface(const char* pName, int* pReturnCode)
{
	return g_PhysicsSystem->QueryInterface(pName);
}

static inline bool FixAppSystemGroup(CAppSystemGroup* AppSystemGroup, 
	CSysModule* FindModule, IAppSystem* FindSystem,
	CSysModule* ReplaceModule, IAppSystem* ReplaceSystem)
{
	int Success = 0;
	for (int i = 0; i < AppSystemGroup->m_Modules.Count(); i++)
	{
		CAppSystemGroup::Module_t& app = AppSystemGroup->m_Modules[i];
		if (app.m_pModule == FindModule)
		{
			app.m_pModule = ReplaceModule;
			Success++;
			break;
		}
	}
	for (int i = 0; i < AppSystemGroup->m_Systems.Count(); i++)
	{
		IAppSystem*& appsystem = AppSystemGroup->m_Systems[i];
		if (appsystem == FindSystem)
		{
			appsystem = ReplaceSystem;
			Success++;
			break;
		}
	}
	return Success == 2;
}

extern "C" bool PhysicsSwap()
{
	PhysicsEngine_t& PhysEngine = g_PhysicsEngines[g_PhysicsEngineName];
	PhysicsEngine_t& PhysEngineSwap = g_PhysicsEngines[g_PhysicsEngineSwapName];
	Module_t& PhysModule = g_Modules[VPHYSICS];
	MsgColor(ColorPurple, "Swapping physics engine to %s...", PhysEngineSwap.VisualName);

	DbgColor(ColorPurple, "Fetching original vphysics interface", 0);
	CSysModule* OrigPhysicsHandle = PhysModule.handle;
	IAppSystem* OrigPhysicsSystem = (IAppSystem*)g_EngineInterface(VPHYSICS_INTERFACE_VERSION, nullptr);
	if (!OrigPhysicsSystem)
	{
		MsgColor(ColorRed, "Failed to find vphysics interface %s", VPHYSICS_INTERFACE_VERSION);
		return false;
	}

	DbgColor(ColorPurple, "Setting working directory", 0);
	ScopedWorkingDir_t WorkingDirScope;
	char ModuleDir[_MAX_PATH];

	// set the working dir to the bin folder so the physics engine can find its dependencies there
	WorkingDirScope.Set(g_BinFolder);

	if (g_PhysicsEngineSwapName != HAVOK)
		V_snprintf(ModuleDir, sizeof(ModuleDir), "%s/addons/sourcemod/extensions/bin/%s" DLL_EXT, g_GameFolder, PhysEngineSwap.ModuleName);
	else
		V_snprintf(ModuleDir, sizeof(ModuleDir), "%s/%s" DLL_EXT, g_BinFolder, PhysEngineSwap.ModuleName);
	V_FixSlashes(ModuleDir);

	DbgColor(ColorPurple, "Loading %s", PhysEngineSwap.ModuleName);
	ScopedSysModule_t PhysicsHandleScope;
	if (!PhysicsHandleScope.Load(ModuleDir))
	{
		MsgColor(ColorRed, "Failed to load %s", PhysEngineSwap.ModuleName);
		return false;
	}

	DbgColor(ColorPurple, "Getting vphysics interface %s", VPHYSICS_INTERFACE_VERSION);
	CreateInterfaceFn fn = Sys_GetFactory(PhysicsHandleScope.Handle);
	g_PhysicsSystem = (IAppSystem*)fn(VPHYSICS_INTERFACE_VERSION, nullptr);
	if (!g_PhysicsSystem)
	{
		MsgColor(ColorRed, "Failed to get vphysics factory in %s", PhysEngineSwap.ModuleName);
		return false;
	}

	// Jolt is split into separate SIMD modules, so the working directory must be moved there to find those
	if (g_PhysicsEngineSwapName != HAVOK)
	{
		V_snprintf(ModuleDir, sizeof(ModuleDir), "%s/addons/sourcemod/extensions/bin", g_GameFolder);
		V_FixSlashes(ModuleDir);
		WorkingDirScope.Set(ModuleDir);
	}
	else
	{
		WorkingDirScope.Set(g_BinFolder);
	}

	DbgColor(ColorPurple, "Connecting physics to engine interface", 0);
	if (!g_PhysicsSystem->Connect(g_EngineInterface))
	{
		MsgColor(ColorRed, "Failed to connect vphysics", 0);
		return false;
	}

	DbgColor(ColorPurple, "Initting physics", 0);
	if (g_PhysicsSystem->Init() != INIT_OK)
	{
		MsgColor(ColorRed, "Failed to init vphysics", 0);
		return false;
	}

	DbgColor(ColorPurple, "Querying physics interfaces", 0);
	g_Physics = (IPhysics*)g_PhysicsSystem->QueryInterface(VPHYSICS_INTERFACE_VERSION);
	g_PhysCollision = (IPhysicsCollision*)g_PhysicsSystem->QueryInterface(VPHYSICS_COLLISION_INTERFACE_VERSION);
	g_PhysSurfaceProps = (IPhysicsSurfaceProps*)g_PhysicsSystem->QueryInterface(VPHYSICS_SURFACEPROPS_INTERFACE_VERSION);

	if (!g_Physics || !g_PhysCollision || !g_PhysSurfaceProps)
	{
		g_Physics = nullptr;
		g_PhysCollision = nullptr;
		g_PhysSurfaceProps = nullptr;
		MsgColor(ColorRed, "Failed to fetch required physics interfaces", 0);
		return false;
	}

	// if any errors occur after this point, there is no recovery!
	WorkingDirScope.Done();

	DbgColor(ColorPurple, "Fixing appsystems", 0);
	CAppSystemGroup* AppSystemGroup = g_Dedicated ? g_DedicatedAPI->m_AppSystemGroup->m_pParentAppSystem : g_EngineAPI->m_Info.m_AppSystemGroup;
	if (!FixAppSystemGroup(AppSystemGroup, OrigPhysicsHandle, OrigPhysicsSystem, PhysicsHandleScope.Handle, g_PhysicsSystem))
	{

		MsgColor(ColorRed, "Failed to fixup appsystemgroup!", 0);
		return false;
	}

	MsgColor(ColorPurple, "Loaded %s, patching memory...", PhysEngineSwap.ModuleName);

	// must flush vcollides first before unloading vphysics
	DbgColor(ColorPurple, "Destroying MDL vcollides", 0);
	g_MDLCache->DestroyAllVCollides();

	DbgColor(ColorPurple, "Shutdown original physics", 0);
	OrigPhysicsSystem->Shutdown();
	DbgColor(ColorPurple, "Disconnect original physics", 0);
	OrigPhysicsSystem->Disconnect();
	DbgColor(ColorPurple, "Unload original physics", 0);
	Sys_UnloadModule(OrigPhysicsHandle);

	PhysModule.handle = nullptr;
	DbgColor(ColorPurple, "Killed %s from %p to %p", PhysEngine.ModuleName, PhysModule.base, PhysModule.base + PhysModule.size);

	// reassign pointers to the new locations
	DbgColor(ColorPurple, "Reassign physics pointers", 0);
	g_RefEnginePhysCollision = g_PhysCollision;
	g_RefEnginePhysSurfaceProps = g_PhysSurfaceProps;
	g_RefDatacachePhysCollision = g_PhysCollision;
	g_RefServerPhysicsInterface = PhysicsQueryInterface;

	// reload vcollides
	DbgColor(ColorPurple, "Reloading MDL vcollides", 0);
	g_MDLCache->LoadAllVCollides();

	// refresh client/server physics pointers, also reloads surfaceproperties
	if (!g_Dedicated)
	{
		DbgColor(ColorPurple, "Init physics in client", 0);
		PhysicsDLLInit(PhysicsQueryInterface);
	}
	DbgColor(ColorPurple, "Init physics in server", 0);
	g_PhysicsHookDummy.Init();

	// fetch new module info
	DbgColor(ColorPurple, "Fetching new module info", 0);
	PhysModule.Init(PhysicsHandleScope.Handle);
	PhysicsHandleScope.Done();

	g_PhysicsEngineName = g_PhysicsEngineSwapName;
	phys_engine.SetValue(PhysEngineSwap.VisualName);

	MsgColor(ColorGreen, "Swap OK", 0);
	return true;
}

inline void* GetConCommandFunction(ConCommand* Command)
{
	return *(void**)((byte*)Command + sizeof(ConCommandBase));
}

static bool FindAdminCommandIssueFunc()
{
	ConCommand* Command = g_pCVar->FindCommand("dumpentityfactories");
	if (!Command)
		return false;

	void* Func = GetConCommandFunction(Command);
	if (!Func)
		return false;

	byte* p = (byte*)Func;
	for (uintptr_t i = 0; i < 0x10; i++, p++)
	{
		if (*p == 0xE8) // call
		{
			uintptr_t Addr = (uintptr_t)p + 5 + *(uintptr_t*)(p + 1);
			UTIL_IsCommandIssuedByServerAdmin = (FuncUTIL_IsCommandIssuedByServerAdmin)Addr;
			return true;
		}
	}

	return false;
}

static bool ParseGameData()
{
	// do gamedata parsing ourselves for these reasons:
	// - SM doesnt register datacache or vphysics library as valid
	// - stock Address functionality is inconvenient
	// - independence from the SM infrastructure

	const char* FileName = "PhysSwapper.games.txt";
	char Path[_MAX_PATH], GameDir[_MAX_PATH];
	g_EngineServer->GetGameDir(GameDir, sizeof(GameDir));
	V_snprintf(Path, sizeof(Path), "%s/addons/sourcemod/gamedata/%s", GameDir, FileName);
	V_FixSlashes(Path);

	ScopedFile_t File;
	if (!File.ReadIntoBuffer(Path))
	{
		MsgColor(ColorRed, "Failed to find gamedata (%s)", FileName);
		return false;
	}

	KeyValueRoot Kv;
	KeyValueErrorCode code = Kv.Parse(File.m_Buffer);
	if (code != KeyValueErrorCode::ERROR_NONE)
	{
		MsgColor(ColorRed, "Failed to parse gamedata (%s), kv error code %d", FileName, code);
		return false;
	}

	KeyValue& Data = Kv["Games"][g_GameName];
	KeyValue& Signatures = Data["Signatures"];
	KeyValue& Addresses = Data["!Addresses"];
	bool Ok = true;

	// I by chance managed to write this whole thing with only continue error handling,
	// so I'm gonna wrap each of those into a nice macro :)
	#define ParseError(fmt, ...) { MsgColor(ColorRed, fmt, __VA_ARGS__); Ok = false; continue; }

	for (KeyValue& Signature : Signatures)
	{
		const char* Name = Signature.key.string;
		const char* Library = Signature["library"].value.string;

		KeyValue& PlatformSigKv = Signature[PLATFORM_NAME];
		if (!PlatformSigKv.IsValid())
			{ ConColorMsg(0, ColorRed, "[" "PhysSwapper" "] " "Signature %s is missing platform %s" "\n", Name, "windows"); Ok = false; continue; };

		const char* PlatformSig = PlatformSigKv.value.string;
		int PlatformSigLen = PlatformSigKv.value.length;
		const int MaxPlatformSigLen = 2048;

		if (!PlatformSigLen || ((PlatformSigLen % 4) != 0))
			ParseError("Signature %s is empty or truncated", Name);

		if (PlatformSigLen > MaxPlatformSigLen)
			ParseError("Signature %s is too long", Name);

		Module_t* Module = GetModule(Library);
		if (!Module)
			ParseError("Signature %s has undefined library %s", Name, Library);

		if (g_Dedicated && Module == g_ModuleClient)
			continue;

		// parse hex sequence \xXX\xXX... into byte array
		char Sig[MaxPlatformSigLen / 4];
		int SigLen = 0;
		for (int i = 0; i < PlatformSigLen; i += 4)
		{
			char Hex[3];
			Hex[0] = PlatformSig[i + 2];
			Hex[1] = PlatformSig[i + 3];
			Hex[2] = '\0';
			Sig[SigLen++] = (char)strtol(Hex, nullptr, 16);
		}

		uintptr_t Address = FindSignature(Module, Sig, SigLen);
		if (Address == 0)
			ParseError("Failed to find signature %s in library %s", Name, Library);

		g_Signatures[Name] = Address;
	}

	// init functions from signatures first
	for (BaseRef* BaseRef : g_BaseRefs)
	{
		if (!BaseRef->IsFunction())
			continue;

		if (g_Dedicated && BaseRef->m_Module == CLIENT)
			continue;

		const auto& Signature = g_Signatures.find(BaseRef->m_Name);
		if (Signature == g_Signatures.end())
			ParseError("Failed to init function %s from signature", BaseRef->m_Name);

		BaseRef->Init(Signature->second);
	}

	// now initialize addresses as some may require signatures
	for (KeyValue& Address : Addresses)
	{
		const char* AddressName = Address.key.string;
		if (Address.childCount == 0)
			ParseError("Address %s is empty", AddressName);

		for (KeyValue& Library : Address)
		{
			const char* LibraryName = Library.key.string;
			ModuleName Module = GetModuleByName(LibraryName);
			if (Module == MODULE_LAST)
				ParseError("Address %s has invalid library %s", LibraryName);
			if (g_Dedicated && Module == CLIENT)
				continue;

			BaseRef* DataRef = FindBaseRef(AddressName, Module, false);
			if (!DataRef)
				ParseError("Address %s in library %s is invalid", AddressName, LibraryName);

			KeyValue& Platform = Library[PLATFORM_NAME];
			if (!Platform.IsValid())
				ParseError("Address %s in library %s is missing platform %s", AddressName, PLATFORM_NAME);

			KeyValue& Offset = Platform["offset"];
			if (!Offset.IsValid())
				ParseError("Address %s in library %s has no offset specified", AddressName, PLATFORM_NAME);

			uintptr_t Addr = 0;
			KeyValue& Signature = Library["signature"];
			if (Signature.IsValid())
			{
				const char* SignatureName = Signature.value.string;
				const auto& Signature = g_Signatures.find(SignatureName);
				if (Signature == g_Signatures.end())
					ParseError("Address %s in library %s has invalid signature %s", AddressName, PLATFORM_NAME, SignatureName);

				Addr = Signature->second;

				uintptr_t Ref = Addr + atoi(Offset.value.string);
				if (!g_Modules[Module].IsValidAddress(Ref))
					ParseError("Address %s in library %s is out of bounds (%p)", AddressName, PLATFORM_NAME, Ref);

				DataRef->m_Ref = *(void**)(Ref);
					
				if (!g_Modules[Module].IsValidAddress((uintptr_t)DataRef->m_Ref))
					ParseError("Dereferenced address %s in library %s is out of bounds (%p)", AddressName, PLATFORM_NAME, DataRef->m_Ref);
			}
			else
			{
				KeyValue& BaseAddress = Library["address"];
				if (BaseAddress.IsValid())
				{
					const char* BaseAddressName = BaseAddress.value.string;
					BaseRef* BaseDataRef = FindBaseRef(BaseAddressName, Module, false);
					if (!BaseDataRef)
						ParseError("Address %s in library %s has invalid base address %s", AddressName, LibraryName, BaseAddressName);

					DataRef->m_Ref = (void*)((uintptr_t)BaseDataRef->m_Ref + atoi(Offset.value.string));
					if (!g_Modules[Module].IsValidAddress((uintptr_t)DataRef->m_Ref))
						ParseError("Dereferenced address %s in library %s is out of bounds (%p)", AddressName, PLATFORM_NAME, DataRef->m_Ref);
				}
				else
				{
					ParseError("Address %s in library %s has no signature or base address specified", AddressName, PLATFORM_NAME);
				}
			}
		}
	}

	#undef ParseError

	return Ok;
}

bool PhysicsNeedsSwap(const char* MapName)
{
	// overriden by concommand?
	if (g_PhysicsEngineOverrideName != PHYSICS_NAME_MAX)
	{
		if (g_PhysicsEngineName == g_PhysicsEngineOverrideName)
			return false;

		g_PhysicsEngineSwapName = g_PhysicsEngineOverrideName;
		return true;
	}

	// check if map or server wants to change it
	if (MapName[0] != '\0')
	{
		char BaseMapName[_MAX_PATH];
		V_FileBase(MapName, BaseMapName, sizeof(BaseMapName));

		char CfgName[_MAX_PATH];
		V_snprintf(CfgName, sizeof(CfgName), "maps%c%s_physics.cfg", CORRECT_PATH_SEPARATOR, BaseMapName);
		FileHandle_t f = g_FileSystem->Open(CfgName, "r", "GAME");
		if (f != nullptr)
		{
			const int MaxSize = 32; // reasonable size
			unsigned int Size = g_FileSystem->Size(f);
			if (Size < MaxSize)
			{
				char PhysicsName[MaxSize];
				g_FileSystem->Read(PhysicsName, Size, f);
				PhysicsName[Size] = '\0';

				for (int i = 0; i < PHYSICS_NAME_MAX; i++)
				{
					const char* VisualName = g_PhysicsEngines[i].VisualName;
					if (!V_stricmp(g_PhysicsEngines[i].VisualName, PhysicsName))
					{
						MsgColor(ColorPurple, "%s specified to use %s physics", CfgName, VisualName);
						if (i == g_PhysicsEngineName)
						{
							g_FileSystem->Close(f);
							return false;
						}

						g_FileSystem->Close(f);
						g_PhysicsEngineSwapName = (PhysicsEngineName)i;
						return true;
					}
				}
			}

			MsgColor(ColorPurple, "%s has invalid physics engine specified", CfgName);
			g_FileSystem->Close(f);
		}
	}

	// not overriden and map/server didnt want to change it, revert to original
	if (g_PhysicsEngineName != g_DefaultPhysicsEngineName)
	{
		MsgColor(ColorPurple, "Reverting to default %s physics", g_PhysicsEngines[g_DefaultPhysicsEngineName].VisualName);
		g_PhysicsEngineSwapName = g_DefaultPhysicsEngineName;
		return true;
	}

	return false;
}

static void COM_TimestampedLog_Wrapper(const char* Format, const char* MapName)
{
	NOTE_UNUSED(Format);

	if (PhysicsNeedsSwap(MapName))
		PhysicsSwap();
}

static bool PatchSpawnServer()
{
	// The complication here is the physics engine must be swapped before any new asset is loaded, but after the BSP data is mounted
	// however detouring a function here is problematic, as they are used in multiple places and not reliable as a "new map load" trigger
	// plus the goal is to minimize the amount of gamedata as well 
	// hence here comes this workaround: look for the call to COM_TimestampedLog("modelloader->GetModelForName(%s)", arg1)
	// then patch a call there to swap the physics engine right before the world model is loaded
	// this log is hidden under normal circumstances and doesn't serve much purpose so we should be good
	byte* p = (byte*)CGameServer__SpawnServer.m_Ref;
	for (uintptr_t i = 0x400; i < 0x500; i++)
	{
		byte* Inst = p + i;

#ifdef _WIN32
		const int32 SkipBytes = 8;
		if (*(int32_t*)Inst != 0x680C75FF) // push [ebp+0C], push offset aModelloaderGet
			continue;
#else
		const int32 SkipBytes = 14;
		if (*(int32_t*)Inst != 0xC7D47D8B) // mov edi, [ebp+0x2C], mov dword ptr [esp], offset aModelloaderGet
			continue;
#endif

		Inst += SkipBytes;

		AsmJump_t Trampoline(0xE8, CalcRelativeJmp(Inst, (void*)COM_TimestampedLog_Wrapper));
		g_SpawnServerPatchAddr = Inst;
		g_SpawnServerTrampoline = PatchMemory<AsmJump_t>(Inst, Trampoline);

#ifdef _WIN32
		// nop out last byte as on windows this call is 6 bytes
		g_SpawnServerTrampolinePad = PatchMemory<byte>(Inst + sizeof(AsmJump_t), 0x90);
#endif

		return true;
	}

	return false;
}

void CPhysicsHook::Init()
{
	// yes, the vtable is smashed here but Init never actually uses anything here
	memset(this, 0, sizeof(*this));
	CPhysicsHook__Init(this);
}

void CMDLCache::DestroyAllVCollides()
{
	MDLHandle_t i = m_Dict.First();
	while (i != m_Dict.InvalidIndex())
	{
		g_MDLCache->DestroyVCollide(i);
		i = m_Dict.Next(i);
	}
}

void CMDLCache::LoadAllVCollides()
{
	MDLHandle_t i = m_Dict.First();
	while (i != m_Dict.InvalidIndex())
	{
		g_MDLCache->UnserializeVCollide(i, true);
		i = m_Dict.Next(i);
	}
}

void CMDLCache::UnserializeVCollide(MDLHandle_t handle, bool noasync)
{
	CMDLCache__UnserializeVCollide(this, handle, noasync);
}

void CMDLCache::DestroyVCollide(MDLHandle_t handle)
{
	CMDLCache__DestroyVCollide(this, handle);
}
	
CON_COMMAND_F(phys_override, "Override physics engine on next map load permanently", FCVAR_GAMEDLL)
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	if (args.ArgC() <= 1)
	{
		MsgColor(ColorYellow, "You must specify a physics engine. Available options:", 0);
		for (int i = 0; i < PHYSICS_NAME_MAX; i++)
		{
			PhysicsEngine_t& PhysEngine = g_PhysicsEngines[i];
			MsgColor(ColorYellow, "\t - %s (%s" DLL_EXT ") %s",
				PhysEngine.VisualName,
				PhysEngine.ModuleName,
				i == g_PhysicsEngineName ? "[active]" : "");
		}
		return;
	}

	PhysicsEngineName PrevPhysicsOverride = g_PhysicsEngineOverrideName;

	const char* PhysicsName = args.Arg(1);
	for (int i = 0; i < PHYSICS_NAME_MAX; i++)
	{
		PhysicsEngine_t& PhysEngine = g_PhysicsEngines[i];
		if (!V_stricmp(PhysEngine.VisualName, PhysicsName))
		{
			if (i == g_PhysicsEngineName)
			{
				MsgColor(ColorYellow, "The current physics engine '%s' is already active", PhysEngine.VisualName);
				return;
			}

			g_PhysicsEngineOverrideName = (PhysicsEngineName)i;
			break;
		}
	}

	if (g_PhysicsEngineOverrideName == PrevPhysicsOverride)
	{
		MsgColor(ColorYellow, "Invalid physics engine name '%s'", PhysicsName);
		return;
	}

	MsgColor(ColorGreen, "Physics engine will be swapped on next map to %s", PhysicsName);
}

static bool LoadPhysSwapper(CreateInterfaceFn engineFactory)
{
	MsgColor(ColorPurple, "Loading...", 0);

	g_EngineInterface = engineFactory;

	g_pCVar = (ICvar*)g_EngineInterface(CVAR_INTERFACE_VERSION, nullptr);
	g_MDLCache = (CMDLCache*)g_EngineInterface(MDLCACHE_INTERFACE_VERSION, nullptr);
	g_EngineServer = (IVEngineServer*)g_EngineInterface(INTERFACEVERSION_VENGINESERVER, nullptr);
	g_FileSystem = (IBaseFileSystem*)g_EngineInterface(BASEFILESYSTEM_INTERFACE_VERSION, nullptr);

	if (!g_pCVar || !g_MDLCache || !g_EngineServer || !g_FileSystem)
	{
		MsgColor(ColorRed, "Failed to find required interfaces", 0);
		return false;
	}

	g_EngineServer->GetGameDir(g_GameFolder, sizeof(g_GameFolder));
	V_FixSlashes(g_GameFolder);

	const char* GameName = V_strrchr(g_GameFolder, CORRECT_PATH_SEPARATOR);
	if (!GameName)
	{
		MsgColor(ColorRed, "Failed to find game name", 0);
		return false;
	}

	V_strcpy(g_GameName, GameName + 1);

	V_strcpy(g_BinFolder, g_GameFolder);
	V_StripLastDir(g_BinFolder, sizeof(g_BinFolder));
	V_strcat(g_BinFolder, "bin", sizeof(g_BinFolder));

	for (Module_t& module : g_Modules)
	{
		char Name[_MAX_PATH];

#ifdef _WIN32
		sprintf(Name, "%s" DLL_EXT, module.name);
#else
		sprintf(Name, "%s_srv" DLL_EXT, module.name);
#endif

		CSysModule* Handle = GetSysModuleHandle(Name);
		if (!Handle)
		{
#ifndef _WIN32
			sprintf(Name, "%s" DLL_EXT, module.name);
			Handle = GetSysModuleHandle(Name);
			if (!Handle)
#endif
			{
				// if theres no client module loaded then assume we are running dedicated
				if (&module == g_ModuleClient)
				{
					g_Dedicated = true;
					continue;
				}

				MsgColor(ColorRed, "Failed to find module info for %s", Name);
				return false;
			}
		}

		module.Init(Handle);
	}

	// This is a lil ugly but its so much to refactor just for this case
#ifndef _WIN32
	if (g_Dedicated)
		g_PhysicsEngines[HAVOK].ModuleName = "vphysics_srv";
#endif

	if (!g_Dedicated)
		g_EngineAPI = (CEngineAPI*)g_EngineInterface(VENGINE_API_VERSION, nullptr);
	else
		g_DedicatedAPI = (CDedicatedServerAPI*)g_EngineInterface(VENGINE_HLDS_API_VERSION, nullptr);

	if (!g_EngineAPI && !g_DedicatedAPI)
	{
		MsgColor(ColorRed, "Failed to find engine API interface", 0);
		return false;
	}

	if (!FindAdminCommandIssueFunc())
	{
		MsgColor(ColorRed, "Failed to find UTIL_IsCommandIssuedByServerAdmin", 0);
		return false;
	}

	if (!ParseGameData())
	{
		MsgColor(ColorRed, "Failed to find required gamedata", 0);
		return false;
	}

	if (!PatchSpawnServer())
	{
		MsgColor(ColorRed, "Failed to inline patch CGameServer::SpawnServer", 0);
		return false;
	}

	ConVar_Register();
	phys_engine.SetValue(g_PhysicsEngines[g_DefaultPhysicsEngineName].VisualName);
		
	MsgColor(ColorGreen, "Loaded successfully", 0);
	return true;
}

static void UnloadPhysSwapper()
{
	if (g_PhysicsEngineName != g_DefaultPhysicsEngineName)
	{
		MsgColor(ColorYellow, "WARNING: Plugin unloaded while physics engine is non-default (%s). It cannot be reverted until the server is restarted",
			g_PhysicsEngines[g_PhysicsEngineName].VisualName);
	}

	ConVar_Unregister();

	if (g_SpawnServerPatchAddr != nullptr)
	{
		PatchMemory<AsmJump_t>(g_SpawnServerPatchAddr, g_SpawnServerTrampoline);
#ifdef _WIN32
		PatchMemory<byte>((byte*)g_SpawnServerPatchAddr + sizeof(AsmJump_t), g_SpawnServerTrampolinePad);
#endif
	}
}

// standard plugin compatibility

class PhysSwapperPlugin : public IServerPluginCallbacks
{
public:
	virtual bool			Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory) override;
	virtual void			Unload(void) override;
	virtual void			Pause(void) {}
	virtual void			UnPause(void) {}
	virtual const char*		GetPluginDescription(void) override { return "Swap physics engines at runtime!"; }
	virtual void			LevelInit(char const* pMapName) {}
	virtual void			ServerActivate(edict_t* pEdictList, int edictCount, int clientMax) {}
	virtual void			GameFrame(bool simulating) {}
	virtual void			LevelShutdown(void) {}
	virtual void			ClientActive(edict_t* pEntity) {}
	virtual void			ClientDisconnect(edict_t* pEntity) {}
	virtual void			ClientPutInServer(edict_t* pEntity, char const* playername) {}
	virtual void			SetCommandClient(int index) {}
	virtual void			ClientSettingsChanged(edict_t* pEdict) {}
	virtual PLUGIN_RESULT	ClientConnect(bool* bAllowConnect, edict_t* pEntity, const char* pszName, const char* pszAddress, char* reject, int maxrejectlen) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT	ClientCommand(edict_t* pEntity, const CCommand& args) { return PLUGIN_CONTINUE; }
	virtual PLUGIN_RESULT	NetworkIDValidated(const char* pszUserName, const char* pszNetworkID) { return PLUGIN_CONTINUE; }
	virtual void			OnQueryCvarValueFinished(QueryCvarCookie_t iCookie, edict_t* pPlayerEntity, EQueryCvarValueStatus eStatus, const char* pCvarName, const char* pCvarValue) {}
	virtual void			OnEdictAllocated(edict_t* edict) {}
	virtual void			OnEdictFreed(const edict_t* edict) {}
};

PhysSwapperPlugin g_PhysSwapperPlugin;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(PhysSwapperPlugin, IServerPluginCallbacks, INTERFACEVERSION_ISERVERPLUGINCALLBACKS, g_PhysSwapperPlugin);

bool PhysSwapperPlugin::Load(CreateInterfaceFn interfaceFactory, CreateInterfaceFn gameServerFactory)
{
	return LoadPhysSwapper(interfaceFactory);
}

void PhysSwapperPlugin::Unload()
{
	UnloadPhysSwapper();
}

// sourcemod compatibility

PhysSwapperExtension g_PhysSwapperExtension;
SMEXT_LINK(&g_PhysSwapperExtension);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(PhysSwapperExtension, SDKExtension, METAMOD_PLAPI_NAME, g_PhysSwapperExtension);

bool PhysSwapperExtension::SDK_OnLoad(char* error, size_t maxlen, bool late)
{
	return LoadPhysSwapper(g_SMAPI->GetEngineFactory(false));
}

void PhysSwapperExtension::SDK_OnUnload()
{
	UnloadPhysSwapper();
}
