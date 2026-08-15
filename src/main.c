#include <windows.h>
#include <d3d9.h>

#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>
#include <SDL2/SDL.h>

#include <config.h>
#include <gfx.h>
#include <global.h>
#include <input.h>
#include <log.h>
#include <patch.h>
#include <script.h>
#include <qb.h>
#include <gslist/gslist.h>
#include <winsock.h>

#define VERSION_NUMBER_MAJOR 1
#define VERSION_NUMBER_MINOR 0
#define VERSION_NUMBER_FIX 11
#define PARTY_ADDR_GAMENET_MANAGER 0x00ab5394


static char configFile[1024];
static void* local_observe_target = 0;
uint8_t local_observing = 0;
uint8_t voluntary_observing = 0;

static void* (__fastcall* GetLocalPlayer)(void*) = (void*)0x00489ac0;
static uint32_t(__fastcall* IsObserving_)(void*) = (void*)0x00491560;

typedef void* (__fastcall* FirstPlayerInfo_t)(void* gameNetManager, int unused, void* searchCtx, char flag);
typedef void* (__fastcall* NextPlayerInfo_t)(void* searchCtx);
static FirstPlayerInfo_t FirstPlayerInfo = (FirstPlayerInfo_t)0x00489730;
static NextPlayerInfo_t  NextPlayerInfo = (NextPlayerInfo_t)0x00432b10;

typedef uint32_t(__fastcall* IsLocalPlayer_t)(void*);
static IsLocalPlayer_t IsLocalPlayer_ = (IsLocalPlayer_t)0x00491540;

typedef void(__fastcall* SetCamMode_t)(void* cameraComponent, int unused, int mode, float param);
static SetCamMode_t SetCamMode = (SetCamMode_t)0x004d9bf0;

typedef void(__fastcall* SetCamSkater_t)(void* cameraComponent, int unused, void* skater);
static SetCamSkater_t SetCamSkater = (SetCamSkater_t)0x004dc310;

typedef void* (__fastcall* GetCamSkater_t)(void* cameraComponent);
static GetCamSkater_t GetCamSkater = (GetCamSkater_t)0x004dc320;


static void* GetCameraComponent(void* self, void** outSkater) {
	void* skater = *(void**)((uint8_t*)self + 0x14);
	if (outSkater) *outSkater = skater;
	return skater ? *(void**)((uint8_t*)skater + 0x37d4) : 0;
}

int __cdecl CFunc_ObserveSelf(CStruct* params) {
    void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
    void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
    if (!self) { printLog("CFunc_ObserveSelf: no local player\n"); return 1; }

    void* mySkater = 0;
    void* cam = GetCameraComponent(self, &mySkater);
    if (!cam || !mySkater) { printLog("CFunc_ObserveSelf: missing cam or skater\n"); return 1; }

    SetCamMode(cam, 0, 2, 0.0f);
    SetCamSkater(cam, 0, mySkater);
    local_observe_target = 0;
    local_observing = 0;
    voluntary_observing = 0;

    return 1;
}

int __cdecl CFunc_IsBetterObserving(CStruct* params) {
    return local_observing;
}

int __cdecl CFunc_IsVoluntaryObserving(CStruct* params) {
	return voluntary_observing;
}

// Happens on game starts and ends, which desyncs tracking
// Function runs every frame to check who you're observing vs tracked target, snaps back to target if mismatch
static uint8_t camera_snapped = 0;
void SnapObsCameraBack(void) {
	if (!local_observing || !local_observe_target) { camera_snapped = 0; return; }

	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) return;

	void* mySkater = 0;
	void* cam = GetCameraComponent(self, &mySkater);
	if (!cam || !mySkater) return;

	void* targetSkater = *(void**)((uint8_t*)local_observe_target + 0x14);
	if (!targetSkater) return;

	void* current = GetCamSkater(cam);
	if (current == mySkater && current != targetSkater) 
	{
		if (camera_snapped) 
		{
			printLog("SnapObsCameraBack: camera was reset to self, reapplying target=%p\n", local_observe_target);
		}
		SetCamMode(cam, 0, 2, 0.0f);
		SetCamSkater(cam, 0, targetSkater);
		camera_snapped = 0;
	} 
	else 
	{
		camera_snapped = !(current == mySkater);
	}
}

int __cdecl CFunc_BetterObserve(CStruct* params) {
	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) { printLog("CFunc_BetterObserve: no local player\n"); return 1; }

	void* target = 0;
	struct { void* vtable; void* dummy; } searchCtx = { (void*)0x0058aa94, 0 };
	void* p = FirstPlayerInfo(gamenetManager, 0, &searchCtx, '\x01');
	while (p != 0)
	{
		if (p != self && !IsLocalPlayer_(p) && !IsObserving_(p)) { target = p; break; }
		p = NextPlayerInfo(&searchCtx);
	}
	if (!target) { printLog("CFunc_BetterObserve: no other active player found\n"); return 1; }

	void* targetSkater = *(void**)((uint8_t*)target + 0x14);
	if (!targetSkater) { printLog("CFunc_BetterObserve: target has no skater\n"); return 1; }

	void* mySkater = 0;
	void* cam = GetCameraComponent(self, &mySkater);
	if (!cam || !mySkater) { printLog("CFunc_BetterObserve: missing cam or own skater\n"); return 1; }

	SetCamMode(cam, 0, 2, 0.0f);
	SetCamSkater(cam, 0, targetSkater);
	local_observing = 1;
	local_observe_target = target;
	voluntary_observing = 1;
	return 1;
}

// Same as CFunc_BetterObserve, but unsets voluntary flag to indicate that we need to leave obs on game end
int __cdecl CFunc_ObserveAfter0(CStruct* params) {
	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) { printLog("CFunc_BetterObserve: no local player\n"); return 1; }

	void* target = 0;
	struct { void* vtable; void* dummy; } searchCtx = { (void*)0x0058aa94, 0 };
	void* p = FirstPlayerInfo(gamenetManager, 0, &searchCtx, '\x01');
	while (p != 0)
	{
		if (p != self && !IsLocalPlayer_(p) && !IsObserving_(p)) { target = p; break; }
		p = NextPlayerInfo(&searchCtx);
	}
	if (!target) { printLog("CFunc_BetterObserve: no other active player found\n"); return 1; }

	void* targetSkater = *(void**)((uint8_t*)target + 0x14);
	if (!targetSkater) { printLog("CFunc_BetterObserve: target has no skater\n"); return 1; }

	void* mySkater = 0;
	void* cam = GetCameraComponent(self, &mySkater);
	if (!cam || !mySkater) { printLog("CFunc_BetterObserve: missing cam or own skater\n"); return 1; }

	SetCamMode(cam, 0, 2, 0.0f);
	SetCamSkater(cam, 0, targetSkater);
	local_observing = 1;
	local_observe_target = target;
	voluntary_observing = 0;

	return 1;
}


int ObserveCamCycle(int direction) {
	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) { printLog("ObserveCamCycle: no local player\n"); return 1; }

	void* mySkater = 0;
	void* cam = GetCameraComponent(self, &mySkater);
	if (!cam || !mySkater) { printLog("ObserveCamCycle: missing cam or own skater\n"); return 1; }

	void* players[8];
	int count = 0;
	players[count++] = self;

	struct { void* vtable; void* dummy; } searchCtx = { (void*)0x0058aa94, 0 };
	void* p = FirstPlayerInfo(gamenetManager, 0, &searchCtx, '\x01');
	while (p != 0 && count < 8) 
	{
		if (p != self && !IsLocalPlayer_(p) && !IsObserving_(p)) {players[count++] = p;}
		p = NextPlayerInfo(&searchCtx);
	}

	if (count <= 1) { printLog("ObserveCamCycle: no other active players to cycle to\n"); return 1; }

	int current = 0;
	if (local_observe_target) 
	{
		for (int i = 0; i < count; i++) 
		{
			if (players[i] == local_observe_target) { current = i; break; }
		}
	}
	int newIndex = ((current + direction) % count + count) % count;
	void* target = players[newIndex];
	bool willBeSelf = (newIndex == 0);

	void* targetSkater = willBeSelf ? mySkater : *(void**)((uint8_t*)target + 0x14);
	if (!targetSkater) { printLog("ObserveCamCycle: target has no skater\n"); return 1; }

	SetCamMode(cam, 0, 2, 0.0f);
	SetCamSkater(cam, 0, targetSkater);
	local_observe_target = willBeSelf ? 0 : target;

	return 1;
}



typedef void(__fastcall* WriteCamFlagByte_t)(void* cameraComponent, int unused, uint8_t param);
static WriteCamFlagByte_t WriteCamFlagByte = (WriteCamFlagByte_t)0x004d9b80;
 
int __cdecl CFunc_DisableLocalPlayerInput(CStruct* params) {
	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) { printLog("CFunc_DisableLocalPlayerInput: no local player\n"); return 1; }
 
	void* mySkater = *(void**)((uint8_t*)self + 0x14);
	if (!mySkater) { printLog("CFunc_DisableLocalPlayerInput: no skater\n"); return 1; }
 
	*(uint8_t*)((uint8_t*)mySkater + 0x97a) = 1;
	return 1;
}
 
int __cdecl CFunc_EnableLocalPlayerInput(CStruct* params) {
	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) { printLog("CFunc_EnableLocalPlayerInput: no local player\n"); return 1; }
 
	void* mySkater = *(void**)((uint8_t*)self + 0x14);
	if (!mySkater) { printLog("CFunc_EnableLocalPlayerInput: no skater\n"); return 1; }
 
	*(uint8_t*)((uint8_t*)mySkater + 0x97a) = 0;
 
	if (*(void**)((uint8_t*)mySkater + 0x37c4) == 0) {
		void* cam = *(void**)((uint8_t*)mySkater + 0x37d4);
		if (cam) WriteCamFlagByte(cam, 0, 1);
	}
	return 1;
}

void ObsInputDisabled(void) {
	if (!local_observing) return;

	void* gamenetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
	void* self = gamenetManager ? GetLocalPlayer(gamenetManager) : 0;
	if (!self) return;

	void* mySkater = *(void**)((uint8_t*)self + 0x14);
	if (!mySkater) return;

	*(uint8_t*)((uint8_t*)mySkater + 0x97a) = 1;
}


void patchSerialCheck(void) {
	patchJmp((void*)0x0048224e, (void*)0x00482394);
	patchNop((void*)(0x0048224e + 5), 1);
}

typedef int(__cdecl* CFunc_PrintStruct_t)(CStruct *, int);
static CFunc_PrintStruct_t CFunc_PrintStruct = (CFunc_PrintStruct_t)0x0041a4c0;

typedef int(__cdecl* CFunc_Change_t)(CStruct *);
static CFunc_Change_t CFunc_Change = (CFunc_Change_t)0x0050f630;

int __cdecl CFunc_GetIniBool(CStruct *params) {
	char *section = "";
	if (!CStruct_GetString(params, 0xd28c8510, &section, 0)) {
		printLog("GetIniBool missing param \"section\" (0xd28c8510)\n");
		return 0;
	}

	char *key = "";
	if (!CStruct_GetString(params, 0x756f5456, &key, 0)) {
		printLog("GetIniBool missing param \"key\" (0x756f5456)\n");
		return 0;
	}

	return getIniBool(section, key, 0, configFile);
}

int __cdecl CFunc_GetIniInteger(CStruct *params, CScript *script) {
	char *section = "";
	if (!CStruct_GetString(params, 0xd28c8510, &section, 0)) {
		printLog("GetIniInteger missing param \"section\" (0xd28c8510)\n");
		return 0;
	}

	char *key = "";
	if (!CStruct_GetString(params, 0x756f5456, &key, 0)) {
		printLog("GetIniInteger missing param \"key\" (0x756f5456)\n");
		return 0;
	}

	uint32_t value_name_checksum = 0;
	if (!CStruct_GetChecksum(params, 0xbf4212ef, &value_name_checksum, 0)) {
		// NOTE: checksum is for lowercase "valuename"; seemingly case insensitive
		printLog("GetIniInteger missing param \"ValueName\" (0xbf4212ef)\n");
		return 0;
	}

	float def_f = 0;
	CStruct_GetFloat(params, 0xcee685bd, &def_f, 0); // "fallback" (0xcee685bd)
	int def = (int)def_f;
	int ini_value = GetPrivateProfileInt(section, key, def, configFile);

	CStruct *out = CScript_GetParams(script);
	CStruct_AddInteger(out, value_name_checksum, ini_value);

	return 1;
}

int __cdecl CFunc_SetIniBool(CStruct *params, CScript *script) {
	char *section = "";
	if (!CStruct_GetString(params, 0xd28c8510, &section, 0)) {
		printLog("SetIniBool missing param \"section\" (0xd28c8510)\n");
		return 0;
	}

	char *key = "";
	if (!CStruct_GetString(params, 0x756f5456, &key, 0)) {
		printLog("SetIniBool missing param \"key\" (0x756f5456)\n");
		return 0;
	}

	float value = 0;
	if (!CStruct_GetFloat(params, 0xe288a7cb, &value, 0)) {
		printLog("SetIniBool missing param \"value\" (0xe288a7cb)\n");
		return 0;
	}

	char *value_str;
	if ((int)value) {
		value_str = "1";
	} else {
		value_str = "0";
	}

	WritePrivateProfileStringA(section, key, value_str, configFile);

	return 1;
}

int __cdecl CFunc_SetIniInteger(CStruct *params, CScript *script) {
	char *section = "";
	if (!CStruct_GetString(params, 0xd28c8510, &section, 0)) {
		printLog("SetIniInteger missing param \"section\" (0xd28c8510)\n");
		return 0;
	}

	char *key = "";
	if (!CStruct_GetString(params, 0x756f5456, &key, 0)) {
		printLog("SetIniInteger missing param \"key\" (0x756f5456)\n");
		return 0;
	}

	float value = 0;
	if (!CStruct_GetFloat(params, 0xe288a7cb, &value, 0)) {
		printLog("SetIniInteger missing param \"value\" (0xe288a7cb)\n");
		return 0;
	}

	char value_str[1024];
	sprintf(value_str, "%d", (int)value);
	WritePrivateProfileStringA(section, key, &value_str, configFile);

	return 1;
}

// allows you to
// ```cscript
// ChangeGlobal name = <name> value = <value>
// ```
// since you can't
// ```cscript
// Change <name> = <value>
// ```
int __cdecl CFunc_ChangeGlobal(CStruct *params, CScript *script) {
	uint32_t name = 0;
	if (!CStruct_GetChecksum(params, 0xa1dc81f9, &name, 0)) {
		printLog("ChangeGlobal missing param \"name\" (0xa1dc81f9)\n");
		return 0;
	}
	CStruct_RemoveComponent(params, 0xa1dc81f9);

	float float_value = 0;
	uint32_t checksum_value = 0;
	if (CStruct_GetFloat(params, 0xe288a7cb, &float_value, 0)) {
		CStruct_RemoveComponent(params, 0xe288a7cb);
		CStruct_AddFloat(params, name, float_value);
	} else if (CStruct_GetChecksum(params, 0xe288a7cb, &checksum_value, 0)) {
		CStruct_RemoveComponent(params, 0xe288a7cb);
		CStruct_AddChecksum(params, name, checksum_value);
	} else {
		printLog("ChangeGlobal missing param \"value\" (0xe288a7cb)\n");
		return 0;
	}

	CFunc_Change(params);

	return 1;
}

int __cdecl CFunc_GetServerList(CStruct *params, CScript *script) {
	// XXX (ellie): Max 256 servers, 256 bytes each, potential for buffer overflow but surely we'll be fine... right?
	char servers[256][256] = {{0}};
	uint32_t num_servers = 0;

	gslist("thps4pc", "\\hostname\\gamever\\gametype\\gamemode\\mapname\\numplayers", servers, &num_servers);

	CArray *array = CArray_New();
	CArray_SetSizeAndType(array, num_servers, TYPE_STRUCTURE);

	for (int i = 0; i < num_servers; i++) {
		char ip[16] = "",
		     hostname[64] = "",
			 gamever[64] = "",
		     gametype[64] = "",
		     gamemode[64] = "",
			 mapname[64] = "",
		     numplayers[64] = "";
		uint32_t port = 0;

		char *server = servers[i];
		// TODO: use sscanf_s
		sscanf(server, "%[^:]:%d \\hostname\\%[^\\]\\gamever\\%[^\\]\\gametype\\%[^\\]\\gamemode\\%[^\\]\\mapname\\%[^\\]\\numplayers\\%[^\\]", ip, &port, hostname, gamever, gametype, gamemode, mapname, numplayers);

		printLog("Server %d (%d chars): %s\n", i, strlen(server), server);
		printLog("Server %d: %s:%d hostname=%s gamever=%s gametype=%s gamemode=%s mapname=%s numplayers=%s\n", i, ip, port, hostname, gamever, gametype, gamemode, mapname, numplayers);

		CStruct *struc = CStruct_New();
		CStruct_AddInteger(struc, 0x7f8c98fe/*index*/, i);
		CStruct_AddString(struc, 0x5a1c4cd2/*ip*/, ip);
		CStruct_AddInteger(struc, 0xbc6ea233/*port*/, port);
		CStruct_AddString(struc, 0x1aae3fee/*hostname*/, hostname);
		CStruct_AddString(struc, 0x748da1c8/*gamever*/, gamever);
		CStruct_AddString(struc, 0x2510a2e9/*gametype*/, gametype);
		CStruct_AddString(struc, 0x3e04b26b/*gamemode*/, gamemode);
		CStruct_AddString(struc, 0xcdef908e/*mapname*/, mapname);
		CStruct_AddString(struc, 0x99a30c62/*numplayers*/, numplayers);
		CArray_SetStructure(array, i, struc);
		// CStruct_Free(struc);
	}

	CStruct *out = CScript_GetParams(script);
	CStruct_AddArray(out, 0x30b77607/*server_list*/, array);
	CArray_Free(array);

	return 1;
}

// FIXME: still broken, not sure why
double ledgeWarpFix(double n) {
	//printf("DOING LEDGE WARP FIX\n");
	//double (__cdecl *orig_acos)(double) = (void *)0x00574ad0;

	__asm {
		sub esp,0x08
		fst qword ptr [esp - 0x08]

		ftst
		jl negative
		fld1
		fcom
		fstp st(0)
		jle end
		fstp st(0)
		fld1
		jmp end
	negative:
		fchs
		fld1
		fcom
		fstp st(0)
		fchs
		jle end
		fstp st(0)
		fld1
		fchs
	end:
		
		add esp,0x08

	}

	callFunc(0x00574ad0);

	//return orig_acos(n);
}

void patchLedgeWarp() {
	patchCall(0x004bc32b, ledgeWarpFix);
}

void __fastcall do_ground_friction(void *skater) {
	//printf("DOING FRICTION FIX\n");

	uint8_t (__fastcall *handle_slope)(void *) = (void *)0x004ba620;
	void (__cdecl *apply_friction)(float *, float, float) = (void *)0x004bb5f0;

	if (!*(int *)(0x0059b680)) {
		*(int *)(0x00aab48c) = 1;
	}

	/*float *vel = *(int *)((int)skater + 0x634) + 0x30;
	float length = sqrtf((vel[0] * vel[0]) + (vel[1] * vel[1]) + (vel[2] * vel[2]));
	float friction = *(float *)((int)skater + 0x37b4);
	float origFriction = *(float *)(*(int *)((int)skater + 0x634) + 0xe0) * 60.0;
	float correctedFriction = *(float *)(*(int *)((int)skater + 0x634) + 0xe0) * *(float *)(*(int *)((int)skater + 0x634) + 0x38) * 60.0;
	float frametime = *(float *)(*(int *)((int)skater + 0x634) + 0x38);
	float unk = *(float *)(*(int *)((int)skater + 0x634) + 0xe0);
	float calcFriction = friction * (1.0 / 60.0) * 60.0 / length;

	printLog("ORIG: %f CORRECTED: %f FRAMETIME: %f UNK: %f FRICTION: %f LENGTH: %f CALCFRICTION: %f\n", origFriction, correctedFriction, frametime, unk, friction, length, calcFriction);*/

	if (!handle_slope(skater)) {
		// do the calculation in double to avoid precision issues
		float *vel = *(int *)((int)skater + 0x634) + 0x30;
		double frictionVector[4];
		for (int i = 0; i < 4; i++) {
			frictionVector[i] = vel[i];
		}

		double length = sqrtf((frictionVector[0] * frictionVector[0]) + (frictionVector[1] * frictionVector[1]) + (frictionVector[2] * frictionVector[2]));

		if (length < 0.0001) {
			vel[0] = 0.0;
			vel[1] = 0.0;
			vel[2] = 0.0;
			vel[3] = 0.0;

			return;
		}

		double friction = *(float *)((int)skater + 0x37b4);
		double frametime = *(float *)(*(int *)((int)skater + 0x634) + 0xe0);

		double calcFriction = (friction * frametime * 60.0) / length;

		frictionVector[0] *= calcFriction;
		frictionVector[1] *= calcFriction;
		frictionVector[2] *= calcFriction;
		frictionVector[3] *= calcFriction;

		double frictionD = (frictionVector[0] * frictionVector[0]) + (frictionVector[1] * frictionVector[1]) + (frictionVector[2] * frictionVector[2]);

		if (frictionD > length * length) {
			vel[0] = 0.0;
			vel[1] = 0.0;
			vel[2] = 0.0;
			vel[3] = 0.0;

			return;
		} else {
			double velocityDouble[4];
			for (int i = 0; i < 4; i++) {
				velocityDouble[i] = vel[i];
			}

			velocityDouble[0] -= frictionVector[0];
			velocityDouble[1] -= frictionVector[1];
			velocityDouble[2] -= frictionVector[2];
			velocityDouble[3] -= frictionVector[3];

			vel[0] = velocityDouble[0];
			vel[1] = velocityDouble[1];
			vel[2] = velocityDouble[2];
			vel[3] = velocityDouble[3];
		}
	}
}

void apply_air_friction(void *skater, float friction) {
	float *vel = *(int *)((int)skater + 0x634) + 0x30;

	double velD = (vel[0] * vel[0]) + (vel[1] * vel[1]) + (vel[2] * vel[2]);
	if (velD < 0.00001f) {
		return;
	}

	double frictionVector[4];
	for (int i = 0; i < 4; i++) {
		frictionVector[i] = vel[i];
	}

	double frametime = *(float *)(*(int *)((int)skater + 0x634) + 0xe0);
	double scale = friction * frametime * 60.0 * velD;

	double len = sqrtf((frictionVector[0] * frictionVector[0]) + (frictionVector[1] * frictionVector[1]) + (frictionVector[2] * frictionVector[2]));

	len = scale / len;

	frictionVector[0] *= len;
	frictionVector[1] *= len;
	frictionVector[2] *= len;

	double frictionD = (frictionVector[0] * frictionVector[0]) + (frictionVector[1] * frictionVector[1]) + (frictionVector[2] * frictionVector[2]);
	if (frictionD > velD) {
		vel[0] = 0.0;
		vel[1] = 0.0;
		vel[2] = 0.0;
		vel[3] = 0.0;
	} else {
		double velocityDouble[4];
		for (int i = 0; i < 4; i++) {
			velocityDouble[i] = vel[i];
		}

		velocityDouble[0] -= frictionVector[0];
		velocityDouble[1] -= frictionVector[1];
		velocityDouble[2] -= frictionVector[2];
		velocityDouble[3] -= frictionVector[3];

		vel[0] = velocityDouble[0];
		vel[1] = velocityDouble[1];
		vel[2] = velocityDouble[2];
		vel[3] = velocityDouble[3];
	}
}

float getScriptFloat(uint32_t sum, int def) {
	float (__fastcall *getFloat)(uint32_t, int) = (void *)0x00419610;
	float result;

	__asm {
		push def
		push sum
		call getFloat
		fstp result
	}

	return result;
}

void __fastcall do_air_friction(void *skater) {
	float (__fastcall *getFloat)(uint32_t) = (void *)0x00419610;

	float crouch_friction = getScriptFloat(0xbed96eda, 0);
	float stand_friction = getScriptFloat(0x1a78b6fc, 0);

	float friction_override_time = *(float *)((int)skater + 0x337c);
	if (friction_override_time != 0.0f) {
		crouch_friction = *(float *)((int)skater + 0x3388);	// friction override value
		stand_friction = crouch_friction;
	}

	uint8_t crouching = *(uint8_t *)((int)skater + 0x33dc);
	if (crouching) {
		apply_air_friction(skater, crouch_friction);
	} else {
		apply_air_friction(skater, stand_friction);
	}
}

void __fastcall speed_limiter(void *skater) {
	float (__fastcall *getFloat)(uint32_t) = (void *)0x00419610;
	float friction = getScriptFloat(0x850eb87a, 0);

	apply_air_friction(skater, friction);
}

void patchFriction() {
	//patchTimer();
	//patchCall(0x004c9946, get_fake_timer);

	patchCall(0x004c0131, do_air_friction);
	patchCall(0x004c0138, do_ground_friction);

	// speed limiter
	patchNop(0x004bb0df, 174);
	patchByte(0x004bb0df, 0x89);
	patchByte(0x004bb0df + 1, 0xf1);
	patchCall(0x004bb0df + 2, speed_limiter);

	
	//patchNop(0x004c0138, 5);
}

void patchDisableGamma();
void patchFrameCap();

uint32_t rng_seed = 0;


char domainStr[256];
char masterServerStr[266];

void patchOnlineService(char *configFile) {
	GetPrivateProfileString("Miscellaneous", "OnlineDomain", "openspy.net", domainStr, 256, configFile);

	sprintf(masterServerStr, "%%s.master.%s", domainStr);
	//printf("TEST: %s\n", masterServerStr);

	patchDWord(0x00544a1c + 1, masterServerStr);

	printLog("Patched online server: %s\n", domainStr);
}

void initPatch() {
	GetModuleFileName(NULL, &executableDirectory, filePathBufLen);

	// find last slash
	char *exe = strrchr(executableDirectory, '\\');
	if (exe) {
		*(exe + 1) = '\0';
	}

	sprintf(configFile, "%s%s", executableDirectory, CONFIG_FILE_NAME);

	int isDebug = getIniBool("Miscellaneous", "Debug", 0, configFile);

	if (isDebug) {
		initializeLogging();
	}

	printLog("PARTYMOD for THPS4 %d.%d.%d\n", VERSION_NUMBER_MAJOR, VERSION_NUMBER_MINOR, VERSION_NUMBER_FIX);
	printLog("DIRECTORY: %s\n", executableDirectory);

	//patchResolution();

	initScriptPatches();

	/*int disableMovies = getIniBool("Miscellaneous", "NoMovie", 0, configFile);
	if (disableMovies) {
		printLog("Disabling movies\n");
		patchNoMovie();
	}*/

	int disableGamma = getIniBool("Graphics", "DisableFullscreenGamma", 1, configFile);
	if (disableGamma) {
		patchDisableGamma();
	}

	int disablePhysicsFixes = getIniBool("Miscellaneous", "DisablePhysicsFixes", 0, configFile);
	if (!disablePhysicsFixes) {
		patchLedgeWarp();
		patchFriction();
	}

	int disableFramerateCap = getIniBool("Miscellaneous", "DisableFramerateCap", 0, configFile);
	if (!disableFramerateCap) {
		patchFrameCap();
	}

	int disableGrass = getIniBool("Graphics", "DisableGrassEffect", 0, configFile);
	if (disableGrass) {
		patchNoGrass();
	}

	int disableVSync = getIniBool("Graphics", "DisableVSync", 0, configFile);
	if (!disableVSync) {
		patchVSync();
	}

	patchOnlineService(configFile);

	// get some source of entropy for the music randomizer
	rng_seed = time(NULL) & 0xffffffff;
	srand(rng_seed);

	initCFuncs();
	addCFunc("ObserveSelf", (void *)CFunc_ObserveSelf);
	addCFunc("IsBetterObserving", (void *)CFunc_IsBetterObserving);
	addCFunc("ObserveAfter0", (void*)CFunc_ObserveAfter0);
	addCFunc("BetterObserve", (void *)CFunc_BetterObserve);
	addCFunc("IsVoluntaryObserving", (void*)CFunc_IsVoluntaryObserving);
	addCFunc("DisableLocalPlayerInput", (void *)CFunc_DisableLocalPlayerInput);
	addCFunc("EnableLocalPlayerInput", (void *)CFunc_EnableLocalPlayerInput);
	addCFunc("GetIniBool", (void *)CFunc_GetIniBool);
	addCFunc("GetIniInteger", (void *)CFunc_GetIniInteger);
	addCFunc("SetIniBool", (void *)CFunc_SetIniBool);
	addCFunc("SetIniInteger", (void *)CFunc_SetIniInteger);
	addCFunc("ChangeGlobal", (void *)CFunc_ChangeGlobal);
	addCFunc("SetSpinKeysControl", (void *)CFunc_SetSpinKeysControl);
	addCFunc("SetSpineTransferControl", (void *)CFunc_SetSpineTransferControl);
	addCFunc("GetServerList", (void *)CFunc_GetServerList);
	addCFunc("SetPauseOnUnfocus", (void *)CFunc_SetPauseOnUnfocus);
	if (isDebug) {
	    printCFuncs();
	}
	patchCFuncs();

	printLog("Patch Initialized\n");
}

uint8_t did_logic = 0;
void __fastcall do_system_logic(void *stack, void *padding, uint8_t is_profiling) {
	void (__fastcall *process_tasks)(void *, void *, uint8_t) = (void *)0x00406670;

	if (!did_logic) {
		process_tasks(stack, padding, is_profiling);
	}
}

void __fastcall do_game_logic(void *stack, void *padding, uint8_t is_profiling) {
	void (__fastcall *process_tasks)(void *, void *, uint8_t) = (void *)0x00406670;

	if (!did_logic) {
		process_tasks(stack, padding, is_profiling);
	}
	did_logic = !did_logic;
}

void patchLogicRate() {
	patchCall(0x00429423, do_system_logic);
	patchCall(0x00429437, do_game_logic);
}

void safeWait(uint64_t endTime) {
	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	uint64_t safetyThreshold = timerFreq / 1000 * 3;	// 3ms

	uint64_t currentTime = SDL_GetPerformanceCounter();

	//printf("%f, %d, %f\n", (double)(nextTime - currentTime) / timerFreq, inFrame->best_effort_timestamp, timebase);

	while (currentTime < endTime) {
		currentTime = SDL_GetPerformanceCounter();

		//printf("%f\n", timerAccumulator);

		if (endTime - currentTime > safetyThreshold) {
			SDL_Delay(1);
			//printf("BIG yawn!\n");
		}
	}
	//printf("wait error - %fms - %d\n", ((double)(endTime - currentTime) / (double)timerFreq) / 1000.0, endTime - currentTime);
}

uint64_t nextFrame = 0;

void do_frame_cap() {
	uint64_t timerFreq = SDL_GetPerformanceFrequency();
	uint64_t frameTarget = (timerFreq / 60);
	//printf("FREQUENCY: %lld, %lld\n", timerFreq, frameTarget);

	if (!nextFrame || nextFrame < SDL_GetPerformanceCounter()) {
		//printf("missed frame target!!\n");
		nextFrame = SDL_GetPerformanceCounter() + frameTarget;
	} else {
		safeWait(nextFrame);
		nextFrame += frameTarget;
	}
}

void endframewrapper() {
	void (*endframe)() = 0x00461940;

	do_frame_cap();

	endframe();
}

void patchFrameCap() {
	// put endscene before present
	for (uint8_t *i = 0x0042945d; i < 0x0042946b; i++) {
		patchCopyByte(i - 5, i);
	}
	patchCall(0x00429466, 0x00461940);

	patchNop(0x004292a0, 58);	// patch out original, too high framerate cap
	patchCall(0x004292a0, do_frame_cap);
	//patchCall(0x00429466, endframewrapper);
}

void patchIsPs2() {
	patchByte(0x00510e38, 0xeb);
}

void patchPrintf() {
	// these all seem to crash the game.  oh well!
	//patchCall(0x00535ef0, printLog);
	//patchByte(0x00535ef0, 0xe9);	// CALL to JMP
	
	// patchCall(0x00405b60, printLog);
	// patchByte(0x00405b60, 0xe9);	// CALL to JMP
}

void patchScriptPrintf() {
	// Patching OurPrintf (0x00535ef0) or 0x00405b60 gives partial success but crashes
	// the game when entering the main menu. Instead, patch individual call sites in
	// CFuncs::ScriptPrintf (0x0050a1e0) so we can call Printf from qb scripts.
	patchCall(0x0050a3cb, printLog);
	patchCall(0x0050a3e5, printLog);
	patchCall(0x0050a4b5, printLog);
	patchCall(0x0050a4fb, printLog);

	// And for CFuncs::ScriptPrintStruct (0041a4c0)
	patchCall(0x0041a4ce, printLog);
	patchCall(0x0041a4e2, printLog);
	patchCall(0x0041a4f2, printLog);
	patchCall(0x0041a522, printLog);
	patchCall(0x0041a540, printLog);
	patchCall(0x0041a56c, printLog);
	patchCall(0x0041a595, printLog);
	patchCall(0x0041a5ab, printLog);
	patchCall(0x0041a5cf, printLog);
	patchCall(0x0041a5fa, printLog);
	patchCall(0x0041a60c, printLog);
	patchCall(0x0041a62f, printLog);
	patchCall(0x0041a667, printLog);
	patchCall(0x0041a685, printLog);
	patchCall(0x0041a69e, printLog);
	patchCall(0x0041a6ad, printLog);
	patchCall(0x0041a6d1, printLog);
	patchCall(0x0041a6fb, printLog);
	patchCall(0x0041a70b, printLog);
}

void patchButtonsFont() {
	// Font name is always set to `ButtonsXbox` if the `buttons_font` flag is passed to `LoadFont`
	// Patch JZ SHORT to JMP SHORT to skip this condition and always load `buttons_font` by name
	patchByte(0x0046369f, 0xEB);
}

int isCD() {
	return 0;
}

int isNotCD() {
	return 1;
}

void patchCD() {
	patchCall(0x00535f00, isNotCD);
	patchByte(0x00535f00, 0xe9);

	patchCall(0x00543fd0, isCD);
	patchByte(0x00543fd0, 0xe9);
}

void our_random(int out_of) {
	// first, call the original random so that we consume a value.  
	// juuust in case someone wants actual 100% identical behavior between partymod and the original game
	void (__cdecl *their_random)(int) = (void *)0x00402c40;

	their_random(out_of);

	return rand() % out_of;
}

void patchRandomMusic() {
	patchCall(0x0042cb74, our_random);
}

void patchOnlineFixes() {
	patchNop(0x00544b1c, 2);
	patchByte(0x0042d4a6, 0xeb);
}

void patchSelectShift() {
	// experimental patch trying to get freecam
	patchNop(0x00505065, 6);
	patchNop(0x00504fef, 2);
}

/*
	graphics patch planning:
	patch vertex buffer stuff in sMesh::Initialize, sMesh::Clone, ~sMesh, and sMesh::Submit
	Create an object based upon the d3d8 buffer that has the same methods, but wraps it with a central buffer
*/

/*
	Tag Limit Patch
	this patch raises the number of objects that can be tricked on in one combo 
	for graffiti mode from 32 to 512.  this should fit every level in the game

	if this is run online as a client, the server will crash when given more 
	than 32 trick objects.  as a server, the patch runs perfectly, clients get 
	large numbers of trick objects without issue

	the way this works is that the patch replaces the usual pending tricks 
	object with a pointer to the extended one.  every function that deals with 
	that class is wrapped to dereference the pointer first and also deal with a
	larger buffer
*/

#define MAX_PENDING_TRICKS 512

struct FixedPendingTricks {
	uint32_t checksums[MAX_PENDING_TRICKS];
	uint32_t trick_count;
};

void __fastcall CPendingTricks_CPendingTricks_Wrapper(struct FixedPendingTricks **pending_tricks) {
	void (__fastcall * CPendingTricks_CPendingTricks)(struct FixedPendingTricks *) = (void *)0x004e0dc0;

	*pending_tricks = malloc(sizeof(struct FixedPendingTricks));

	(*pending_tricks)->trick_count = 0;
}

uint8_t __fastcall CPendingTricks_FlushTricks_Wrapper(struct FixedPendingTricks **pending_tricks) {
	uint8_t (__fastcall * CPendingTricks_FlushTricks)(struct FixedPendingTricks *) = (void *)0x004e0f90;

	(*pending_tricks)->trick_count = 0;

	return 1;
}

uint32_t lastcount = 0;

uint32_t __fastcall CPendingTricks_TrickOffObject_Wrapper(struct FixedPendingTricks **pending_tricks, void *pad, uint32_t obj) {
	uint32_t (__fastcall * CPendingTricks_TrickOffObject)(struct FixedPendingTricks *, void *, uint32_t) = (void *)0x004e0dd0;

	if ((*pending_tricks)->trick_count > MAX_PENDING_TRICKS) {
		(*pending_tricks)->trick_count = MAX_PENDING_TRICKS;
	}

	return CPendingTricks_TrickOffObject(*pending_tricks, pad, obj);
}

uint32_t __fastcall CPendingTricks_WriteToBuffer_Wrapper(struct FixedPendingTricks **pending_tricks, void *pad, uint32_t *buf, uint32_t size) {
	uint32_t (__fastcall * CPendingTricks_WriteToBuffer)(struct FixedPendingTricks *, void *, uint32_t *, uint32_t) = (void *)0x004e0e90;

	if ((*pending_tricks)->trick_count > MAX_PENDING_TRICKS) {
		(*pending_tricks)->trick_count = MAX_PENDING_TRICKS;
	}

	uint32_t result = CPendingTricks_WriteToBuffer(*pending_tricks, pad, buf, size);

	return result;
}

void __fastcall CGoalManager_Land_Graffiti(void* goal_manager) {
	uint32_t (__fastcall * Skate_GetLocalSkater)(uint32_t) = (void*)0x004fa1e0;
	uint32_t (__fastcall * CSkater_GetScoreObject)(uint32_t) = (void*)0x004b77c0;
	void (__fastcall * CGoalManager_GotTrickObject)(void *, void *, uint32_t, uint32_t) = (void*)0x004eb620;

	uint32_t *skate_instance = (uint32_t *)0x00ab5b48;
	uint32_t local_skater = Skate_GetLocalSkater(*skate_instance);
	uint32_t pScore = CSkater_GetScoreObject(local_skater);
	uint32_t last_score_landed = *((uint32_t *)(pScore + 0x18));
	struct FixedPendingTricks** pending_tricks = (struct FixedPendingTricks**)(local_skater + 0x6f0);

	for (int i = 0; i < (*pending_tricks)->trick_count; i++) {
		CGoalManager_GotTrickObject(goal_manager, NULL, (*pending_tricks)->checksums[i], last_score_landed);
	}
}

void __fastcall Score_LogTrickObject_Wrapper(void *pScore, void *pad, uint32_t skater_id, uint32_t score, uint32_t trick_count, uint32_t* pending_tricks, uint8_t propagate) {
	void (__fastcall * Score_LogTrickObject)(void *, void *, uint32_t, uint32_t, uint32_t, struct FixedPendingTricks**, uint8_t) = (void*)0x004f70d0;

	Score_LogTrickObject(pScore, pad, skater_id, score, trick_count, pending_tricks, propagate);
}

void patchTagLimit() {
	// CPendingTricks::CPendingTricks
	patchCall(0x004cc185, CPendingTricks_CPendingTricks_Wrapper);

	// CPendingTricks::FlushTricks
	patchCall(0x004bc4e3, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004c1a29, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004c2228, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004ce808, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004ce97a, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004d39e3, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004d3a7d, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004d8a67, CPendingTricks_FlushTricks_Wrapper);
	patchCall(0x004d8aa7, CPendingTricks_FlushTricks_Wrapper);

	// CPendingTricks::TrickOffObject
	patchByte(0x004e0ddd, 0xeb);	// remove bounds check from TrickOffObject
	patchNop(0x004e0e6f, 3);	// remove modulo to act as ring buffer.  this may seem unsafe (and it sort of is) but there cannot be more trick objects than the new tag limit, so this will never happen
	patchCall(0x004bca53, CPendingTricks_TrickOffObject_Wrapper);
	patchCall(0x004c5963, CPendingTricks_TrickOffObject_Wrapper);
	patchCall(0x004d8acc, CPendingTricks_TrickOffObject_Wrapper);
	patchCall(0x004d8af0, CPendingTricks_TrickOffObject_Wrapper);
	patchByte(0x004d8af0, 0xe9);	// change prev from CALL to JMP
	
	// adjust offsets
	patchDWord(0x004e0dd4 + 2, MAX_PENDING_TRICKS * sizeof(uint32_t));
	patchDWord(0x004e0e69 + 2, MAX_PENDING_TRICKS * sizeof(uint32_t));
	patchDWord(0x004e0e75 + 2, MAX_PENDING_TRICKS * sizeof(uint32_t));
	patchDWord(0x004e0e7c + 2, MAX_PENDING_TRICKS * sizeof(uint32_t));

	// CPendingTricks::WriteToBuffer
	patchByte(0x004e0ea6, 0xeb);	// remove bounds check from WriteToBuffer
	patchCall(0x004d8a57, CPendingTricks_WriteToBuffer_Wrapper);
	// adjust trick count offset
	patchDWord(0x004e0e99 + 2, MAX_PENDING_TRICKS * sizeof(uint32_t));

	// CGoalManager::Land - replace the graffiti branch with our own logic
	patchNop(0x004edc88, 85);
	patchByte(0x004edc88, 0x8b);	// eax to ecx
	patchByte(0x004edc88 + 1, 0xcb);	// eax to ecx
	patchCall(0x004edc88 + 2, CGoalManager_Land_Graffiti);

	// Score::LogTrickObjectRequest
	patchDWord(0x004f7010 + 2, (MAX_PENDING_TRICKS * sizeof(uint32_t)) + 0x10);	// expand stack to fit new message
	patchDWord(0x004f70ba + 2, (MAX_PENDING_TRICKS * sizeof(uint32_t)) + 0x10);	// stack pointer add
	patchDWord(0x004f7074 + 1, MAX_PENDING_TRICKS * sizeof(uint32_t));	// fix size passed to WritePendingTricks
	patchDWord(0x004f70a9 + 1, MAX_PENDING_TRICKS * sizeof(uint32_t));	// fix size of msg sent to server
	
	// Score::LogTrickObject
	patchDWord(0x004f70e5 + 2, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x48);	// expand stack to fit new message
	patchDWord(0x004f7469 + 2, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x54);	// stack pointer add
	// fix stack pointers
	patchDWord(0x004f7457 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x58);	// fix exception list
	patchDWord(0x004f710f + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x60);

	patchDWord(0x004f7116 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x78);
	patchDWord(0x004f7169 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x6c);
	patchDWord(0x004f7170 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x68);
	patchDWord(0x004f717a + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x74);
	patchDWord(0x004f7189 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x78);
	patchDWord(0x004f71a8 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x70);
	patchDWord(0x004f723f + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x68);
	patchDWord(0x004f7284 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x68);
	patchDWord(0x004f7295 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x6c);
	patchDWord(0x004f729c + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x70);
	patchDWord(0x004f72bf + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x74);
	patchDWord(0x004f7334 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x70);
	patchDWord(0x004f7357 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x74);
	patchDWord(0x004f7410 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x68);
	patchDWord(0x004f7424 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x68);
	patchDWord(0x004f742b + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x70);
	patchDWord(0x004f7438 + 3, ((MAX_PENDING_TRICKS * sizeof(uint32_t)) * 2) + 0x74);

	patchDWord(0x004f73aa + 3, (0xdc - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
	patchDWord(0x004f7365 + 3, (0xd8 - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
	patchDWord(0x004f735e + 3, (0xcc - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
	patchDWord(0x004f7350 + 3, (0xd4 - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
	patchDWord(0x004f7342 + 3, (0xd4 - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
	patchDWord(0x004f733b + 3, (0xd0 - 0x80) + (MAX_PENDING_TRICKS * sizeof(uint32_t)));
}

/*
	End Tag Limit Patch
*/

__declspec(dllexport) BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
	// Perform actions based on the reason for calling.
	switch(fdwReason) { 
		case DLL_PROCESS_ATTACH:
			// Initialize once for each new process.
			// Return FALSE to fail DLL load.

			// install patches
			patchWindow();
			patchInput();
			patchCall((void *)(0x005319ab), &(initPatch));
			patchScriptHook();
			patchScreenFlash();
			patchRandomMusic();
			patchOnlineFixes();

			patchRenderer();

			//patchAnisotropicFilter();

			patchUIPositioning();
			patchMovieBlackBars();

			patchVertexBufferCreation();

			patchTagLimit();

			// New patch
			patchSerialCheck();
			//
		
			//patchSkipSkaterDestroy();
		
			//patchByte((void*)0x488FAB, 12); 
			//patchJmpTest();

			//patchPrintf();
			patchScriptPrintf();
			patchButtonsFont();
			//patchCD();

			break;

		case DLL_THREAD_ATTACH:
			// Do thread-specific initialization.
			break;

		case DLL_THREAD_DETACH:
			// Do thread-specific cleanup.
			break;

		case DLL_PROCESS_DETACH:
			// Perform any necessary cleanup.
			break;
	}
	return TRUE;
}
