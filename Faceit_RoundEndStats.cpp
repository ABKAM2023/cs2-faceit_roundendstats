#include <stdio.h>
#include <map>
#include "Faceit_RoundEndStats.h"
#include "metamod_oslink.h"
#include "schemasystem/schemasystem.h"

Faceit_RoundEndStats g_Faceit_RoundEndStats;
PLUGIN_EXPOSE(Faceit_RoundEndStats, g_Faceit_RoundEndStats);
IVEngineServer2* engine = nullptr;
CGameEntitySystem* g_pGameEntitySystem = nullptr;
CEntitySystem* g_pEntitySystem = nullptr;
CGlobalVars *gpGlobals = nullptr;

IUtilsApi* g_pUtils;
IPlayersApi* g_pPlayers = nullptr;

#define MAXPLAYERS 64

std::map<std::string, std::string> g_vecPhrases;

struct DamageInfo
{
    int iDamage = 0;
    int iHits = 0;
    int iKills = 0;
};

struct PlayerDamageStats
{
    std::map<int, DamageInfo> dealtTo;
    std::map<int, DamageInfo> receivedFrom;
    
    void Reset()
    {
        dealtTo.clear();
        receivedFrom.clear();
    }
};

PlayerDamageStats g_PlayerStats[MAXPLAYERS + 1];

int g_iPlayerHealth[MAXPLAYERS + 1];

CGameEntitySystem* GameEntitySystem()
{
	return g_pUtils->GetCGameEntitySystem();
}

void LoadTranslations()
{
	KeyValues::AutoDelete g_kvPhrases("Phrases");
	const char *pszPath = "addons/translations/faceit_roundendstats.phrases.txt";
	if (!g_kvPhrases->LoadFromFile(g_pFullFileSystem, pszPath))
	{
		ConColorMsg(Color(255, 165, 0, 255), "[Faceit_RoundEndStats] Failed to load %s\n", pszPath);
		return;
	}

	std::string szLanguage = std::string(g_pUtils->GetLanguage());
	const char* g_pszLanguage = szLanguage.c_str();
	for (KeyValues *pKey = g_kvPhrases->GetFirstTrueSubKey(); pKey; pKey = pKey->GetNextTrueSubKey())
		g_vecPhrases[std::string(pKey->GetName())] = std::string(pKey->GetString(g_pszLanguage));
	
	ConColorMsg(Color(0, 255, 0, 255), "[Faceit_RoundEndStats] Loaded %zu phrases\n", g_vecPhrases.size());
}

void ResetAllStats()
{
    for (int i = 0; i <= MAXPLAYERS; i++)
    {
        g_PlayerStats[i].Reset();
        g_iPlayerHealth[i] = 100;
    }
}

void OnRoundStart(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
    ResetAllStats();
}

void OnPlayerHurt(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
    if (!pEvent) return;
    
    int iAttacker = pEvent->GetInt("attacker");
    int iVictim = pEvent->GetInt("userid");
    int iDamage = pEvent->GetInt("dmg_health");
    int iHealth = pEvent->GetInt("health");
    
    CCSPlayerController* pAttackerController = nullptr;
    CCSPlayerController* pVictimController = nullptr;
    
    for (int i = 0; i <= gpGlobals->maxClients; i++)
    {
        CCSPlayerController* pController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance(CEntityIndex(i + 1));
        if (!pController) continue;
        
        CBasePlayerController* pBaseController = (CBasePlayerController*)pController;
        if (!pBaseController) continue;
        
        CPlayerSlot slot = pBaseController->GetPlayerSlot();
        
        int playerUserId = engine->GetPlayerUserId(slot).Get();
        
        if (playerUserId == iAttacker)
            pAttackerController = pController;
        if (playerUserId == iVictim)
            pVictimController = pController;
    }
    
    if (!pAttackerController || !pVictimController) return;
    
    int iAttackerSlot = pAttackerController->GetPlayerSlot();
    int iVictimSlot = pVictimController->GetPlayerSlot();

    if (iAttackerSlot == iVictimSlot) return;
    
    g_PlayerStats[iAttackerSlot].dealtTo[iVictimSlot].iDamage += iDamage;
    g_PlayerStats[iAttackerSlot].dealtTo[iVictimSlot].iHits++;
    
    g_PlayerStats[iVictimSlot].receivedFrom[iAttackerSlot].iDamage += iDamage;
    g_PlayerStats[iVictimSlot].receivedFrom[iAttackerSlot].iHits++;
    
    g_iPlayerHealth[iVictimSlot] = iHealth;
}

void OnPlayerDeath(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
    if (!pEvent) return;
    
    int iAttacker = pEvent->GetInt("attacker");
    int iVictim = pEvent->GetInt("userid");
    
    CCSPlayerController* pAttackerController = nullptr;
    CCSPlayerController* pVictimController = nullptr;
    
    for (int i = 0; i <= gpGlobals->maxClients; i++)
    {
        CCSPlayerController* pController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance(CEntityIndex(i + 1));
        if (!pController) continue;
        
        CBasePlayerController* pBaseController = (CBasePlayerController*)pController;
        if (!pBaseController) continue;
        
        CPlayerSlot slot = pBaseController->GetPlayerSlot();
        int playerUserId = engine->GetPlayerUserId(slot).Get();
        
        if (playerUserId == iAttacker)
            pAttackerController = pController;
        if (playerUserId == iVictim)
            pVictimController = pController;
    }
    
    if (!pAttackerController || !pVictimController) return;
    
    int iAttackerSlot = pAttackerController->GetPlayerSlot();
    int iVictimSlot = pVictimController->GetPlayerSlot();
    
    if (iAttackerSlot == iVictimSlot) return;

    g_PlayerStats[iAttackerSlot].dealtTo[iVictimSlot].iKills++;
    
    g_iPlayerHealth[iVictimSlot] = 0;
}

void OnRoundEnd(const char* szName, IGameEvent* pEvent, bool bDontBroadcast)
{
    for (int i = 0; i <= gpGlobals->maxClients; i++)
    {
        CCSPlayerController* pController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance(CEntityIndex(i + 1));
        if (!pController) continue;
        
        CBasePlayerController* pBaseController = (CBasePlayerController*)pController;
        if (!pBaseController) continue;
        
        int iSlot = pBaseController->GetPlayerSlot();
        
        if (g_pPlayers && !g_pPlayers->IsInGame(iSlot)) continue;
        
        PlayerDamageStats& stats = g_PlayerStats[iSlot];
        
        std::map<int, bool> interactedPlayers;
        
        for (auto& pair : stats.dealtTo)
            interactedPlayers[pair.first] = true;
        for (auto& pair : stats.receivedFrom)
            interactedPlayers[pair.first] = true;
        
        for (auto& pair : interactedPlayers)
        {
            int iOtherSlot = pair.first;
            
            CCSPlayerController* pOtherController = (CCSPlayerController*)g_pEntitySystem->GetEntityInstance(CEntityIndex(iOtherSlot + 1));
            if (!pOtherController) continue;
            
            const char* szOtherName = pOtherController->m_iszPlayerName();
            if (!szOtherName || szOtherName[0] == '\0') continue;
   
            DamageInfo dealt = stats.dealtTo[iOtherSlot];
            DamageInfo received = stats.receivedFrom[iOtherSlot];
            
            int iOtherHealth = g_iPlayerHealth[iOtherSlot];
            
            char szMessage[512];
            snprintf(szMessage, sizeof(szMessage), 
                g_vecPhrases["DamageStats"].c_str(),
                dealt.iDamage, dealt.iHits,
                received.iDamage, received.iHits,
                szOtherName, iOtherHealth);
            
            g_pUtils->PrintToChat(iSlot, szMessage);
        }
    }
}

void StartupServer()
{
	g_pGameEntitySystem = GameEntitySystem();
	g_pEntitySystem = g_pUtils->GetCEntitySystem();
	gpGlobals = g_pUtils->GetCGlobalVars();
	
	ResetAllStats();
}

bool Faceit_RoundEndStats::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pCVar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pSchemaSystem, ISchemaSystem, SCHEMASYSTEM_INTERFACE_VERSION);
	GET_V_IFACE_CURRENT(GetEngineFactory, engine, IVEngineServer2, SOURCE2ENGINETOSERVER_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetFileSystemFactory, g_pFullFileSystem, IFileSystem, FILESYSTEM_INTERFACE_VERSION);

	g_SMAPI->AddListener( this, this );

	return true;
}

bool Faceit_RoundEndStats::Unload(char *error, size_t maxlen)
{
	ConVar_Unregister();
	
	return true;
}

void Faceit_RoundEndStats::AllPluginsLoaded()
{
	char error[64];
	int ret;
	g_pUtils = (IUtilsApi *)g_SMAPI->MetaFactory(Utils_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		g_SMAPI->Format(error, sizeof(error), "Missing Utils system plugin");
		ConColorMsg(Color(255, 0, 0, 255), "[%s] %s\n", GetLogTag(), error);
		std::string sBuffer = "meta unload "+std::to_string(g_PLID);
		engine->ServerCommand(sBuffer.c_str());
		return;
	}
	
	g_pPlayers = (IPlayersApi *)g_SMAPI->MetaFactory(PLAYERS_INTERFACE, &ret, NULL);
	if (ret == META_IFACE_FAILED)
	{
		ConColorMsg(Color(255, 255, 0, 255), "[%s] Players API not found, continuing without player checks\n", GetLogTag());
	}
	
	LoadTranslations();
	g_pUtils->StartupServer(g_PLID, StartupServer);
	
	g_pUtils->HookEvent(g_PLID, "round_start", OnRoundStart);
	g_pUtils->HookEvent(g_PLID, "player_hurt", OnPlayerHurt);
	g_pUtils->HookEvent(g_PLID, "player_death", OnPlayerDeath);
	g_pUtils->HookEvent(g_PLID, "round_end", OnRoundEnd);
}

///////////////////////////////////////
const char* Faceit_RoundEndStats::GetLicense()
{
	return "GPL";
}

const char* Faceit_RoundEndStats::GetVersion()
{
	return "1.0";
}

const char* Faceit_RoundEndStats::GetDate()
{
	return __DATE__;
}

const char *Faceit_RoundEndStats::GetLogTag()
{
	return "[Faceit_RoundEndStats]";
}

const char* Faceit_RoundEndStats::GetAuthor()
{
	return "ABKAM";
}

const char* Faceit_RoundEndStats::GetDescription()
{
	return "Faceit Round End Stats";
}

const char* Faceit_RoundEndStats::GetName()
{
	return "Faceit Round End Stats";
}

const char* Faceit_RoundEndStats::GetURL()
{
	return "https://discord.gg/ChYfTtrtmS";
}
