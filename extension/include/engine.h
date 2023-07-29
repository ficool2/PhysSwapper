class CGameServer;

struct StartupInfo_t
{
	int gap0[4];
	CAppSystemGroup* m_AppSystemGroup;
	int gap1;
};

class CEngineAPI
{
public:
	void** __vtbl;
	unsigned char gap0[10];
	StartupInfo_t m_Info;
	// ...
};

#define VENGINE_API_VERSION "VENGINE_LAUNCHER_API_VERSION004"

class CDedicatedServerAPI
{
public:
	void** __vtbl;
	int gap;
	CAppSystemGroup* m_AppSystemGroup;
	// ...
};

#define VENGINE_HLDS_API_VERSION "VENGINE_HLDS_API_VERSION002"

struct studiodata_t
{
	void* m_Cache;
	vcollide_t m_VCollide;
	// ...
};

class CMDLCache : public IMDLCache
{
public:
	void UnserializeVCollide(MDLHandle_t handle, bool noasync);
	void DestroyVCollide(MDLHandle_t handle);

	void DestroyAllVCollides();
	void LoadAllVCollides();

	int gap[8];
	CUtlDict<studiodata_t*, MDLHandle_t> m_Dict;
	// ...
};

class CPhysicsHook
{
public:
	void Init();

	int gap[16];
	CUtlVector<char*> m_vehicleScripts;
	float m_impactSoundTime;
	bool m_bPaused;
	bool m_isFinalTick;
};