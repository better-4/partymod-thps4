#include <windows.h>
#include <d3d9.h>

#include <stdio.h>
#include <stdint.h>
#include <time.h>

#include <SDL2/SDL.h>

#include <config.h>
#include <gfx.h>
#include <global.h>
#include <input.h>
#include <log.h>
#include <patch.h>
#include <script.h>
#include <qb.h>

#define VERSION_NUMBER_MAJOR 1
#define VERSION_NUMBER_MINOR 0
#define VERSION_NUMBER_FIX 11


// ============================================================================
// OBS Implementations
//
//   High level view for qutting obs:
//   1. Any player clicks "Quit Observing" (QB) -> CFunc_RequestExitObserverMode
//      -> RequestExitObserverMode sends MSG_ID_EXIT_OBSERVER_REQUEST (0x7C)
//      to the host (a loopback send if the caller IS the host).
//
//   2. HOST ONLY: HandleExitObserverRequest receives 0x7C, resolves the real
//      sender, sets PENDING_PLAYER on the host's own authoritative copy of
//      them, and unicasts MSG_ID_EXIT_OBSERVER_PROCEED (0x7D) back to
//      whoever asked.
//
//   3. Whoever receives 0x7D (always exactly the original requester) runs
//      HandleExitObserverProceed -> ExitObserverModeLocal, which sets
//      PENDING_PLAYER on that machine's own local player.
//
//   4. If step 3 happens on the HOST, it ALSO force-transitions every other
//      actively-playing player into real, vanilla observer mode via
//      ForceAllOthersObserving -- because vanilla's LoadPendingPlayers
//      (the function that actually promotes a pending player back into
//      active play) has a confirmed bug: its reconstruction pipeline reads
//      stale/invalid state if the HOST tries to restore itself while
//      anyone else is still actively skating. Forcing everyone else into
//      observer mode first is how we satisfy that precondition.
//
//   5. quit_observing.q (QB script) waits a short delay after triggering
//      step 1, then calls the vanilla LoadPendingPlayers CFunc directly,
//      which destroys and reconstructs every pending player -- the same
//      mechanism a genuinely new player uses to join an in-progress lobby.
//      We never reimplement that reconstruction ourselves.
// 
//   Additonal Patches:
//	 - Removed "now observivng" message with patchSkipNowObservingMsg();
//   - Removed "joining" message for players quitting observer mode with patchSkipObserverJoinMsg(); 
//     (wont appears for other players, but very briefly will for player leaving obs)
//   - Removed serial check that disallowed same cd key players from joining each other with patchSerialCheck();
// ============================================================================

#define PARTY_ADDR_GAMENET_MANAGER      0x00ab5b48   // the GameNetManager singleton -- represents THIS machine's whole live network session (connections, player list, etc). NOT the same object as the local-player singleton below.
#define PARTY_ADDR_GET_LOCAL_PLAYER     0x00489ac0
#define PARTY_FLAG_LOCAL_PLAYER   0x00000001
#define PARTY_FLAG_OBSERVER       0x00000004
#define PARTY_FLAG_PENDING_PLAYER 0x00000008
#define PARTY_FLAG_JUMPING_IN     0x00000010
#define PARTY_FLAG_FULLY_IN       0x00000020
#define PARTY_PLAYERINFO_FLAGS_OFFSET 0xf8
#define PARTY_ADDR_LOCAL_PLAYER_SINGLETON 0x00ab5394   // separate singleton -- "my own local-player context". What GetLocalPlayer/RequestObserverMode/IsHost/LoadPendingPlayers all actually expect. Mixing this up with PARTY_ADDR_GAMENET_MANAGER was the source of an early, hard-to-find crash (garbage pointer -> IEEE-754 bit pattern for pi).
#define PARTY_ADDR_IS_HOST 0x0048ead0
#define PARTY_ADDR_IS_OBSERVING 0x00491560   /* confirmed via JMP-tail trace from IsObserving_cfunc */
#define PARTY_ADDR_RUN_SCRIPT   0x00413420   /* void __cdecl RunScript(const char*, void*, void*, char) */
#define MSG_ID_EXIT_OBSERVER_REQUEST  0x7C   /* client -> host: "let me back in" */
#define MSG_ID_EXIT_OBSERVER_PROCEED  0x7D   /* host -> requester: "proceed" */

static char configFile[1024];

// GetLocalPlayer(gameNetManager or local-player-singleton) -> PlayerInfo* for
// whichever machine calls it. Confirmed __fastcall, single arg in ECX.
static void* (__fastcall* GetLocalPlayer)(void*) = (void*)PARTY_ADDR_GET_LOCAL_PLAYER;

// IsHost() -> nonzero if THIS machine is the session host. Confirmed __cdecl.
static uint32_t(__cdecl* IsHost)(void) = (void*)PARTY_ADDR_IS_HOST;

// IsObserving_(PlayerInfo*) -> nonzero if that player currently has the
// OBSERVER flag set. Confirmed __fastcall; internally just reads
// [player+0xf8] & PARTY_FLAG_OBSERVER.
static uint32_t(__fastcall* IsObserving_)(void*) = (void*)PARTY_ADDR_IS_OBSERVING;

// FirstPlayerInfo/NextPlayerInfo: the engine's own player-list iterator,
// used everywhere in vanilla code (EnterObserverMode, LoadPendingPlayers,
// etc). searchCtx is a small caller-owned struct; its first field must be
// the vtable pointer at data address 0x0058aa94 (copied from every vanilla
// caller of this same pattern), second field is scratch space the iterator
// uses internally. FirstPlayerInfo is confirmed __thiscall (bridged below
// the same way as every other __thiscall native this file calls);
// NextPlayerInfo is confirmed genuinely __fastcall.
typedef void* (__fastcall* FirstPlayerInfo_t)(void* gameNetManager, int unused, void* searchCtx, char flag);
typedef void* (__fastcall* NextPlayerInfo_t)(void* searchCtx);
static FirstPlayerInfo_t FirstPlayerInfo = (FirstPlayerInfo_t)0x00489730;
static NextPlayerInfo_t  NextPlayerInfo = (NextPlayerInfo_t)0x00432b10;

// RunScript(name, params, unk, flag): invokes a QB script by name. Confirmed __cdecl.
typedef void(__cdecl* RunScript_t)(const char*, void*, void*, char);
static RunScript_t RunScript = (RunScript_t)PARTY_ADDR_RUN_SCRIPT;

// ResolvePlayer(gameNetManager, _, connId) -> PlayerInfo* whose stored
// connection ID matches connId. This is how a message handler figures out
// WHO actually sent the message it's processing. Confirmed __thiscall
// (bridged as __fastcall + dummy below).
typedef void* (__fastcall* ResolvePlayerFromConn_t)(void* gameNetManager, int unused, int connId);
static ResolvePlayerFromConn_t ResolvePlayer = (ResolvePlayerFromConn_t)0x004893f0;

// SendMsgToServer(serverConn, _, msgId, ...): a CLIENT sends a message to
// whoever it's connected to as server (the host). Used by a requester,
// host or not, to send its own request. Confirmed __thiscall.
typedef void(__fastcall* SendMsgToServer_t)(void*, int, int, int, void*, int, int, char, char, int);
static SendMsgToServer_t SendMsgToServer = (SendMsgToServer_t)0x004301f0;

// IsLocalPlayer_(PlayerInfo*) -> nonzero if that PlayerInfo represents
// the LOCAL player on the machine currently running this code (i.e. "is
// this me", not "is this the host"). Confirmed __fastcall; reads
// [player+0xf8] & PARTY_FLAG_LOCAL_PLAYER.
typedef uint32_t(__fastcall* IsLocalPlayer_t)(void*);
static IsLocalPlayer_t IsLocalPlayer_ = (IsLocalPlayer_t)0x00491540;

// AddHandler(dispatcher, _, msgId, handlerFn, priority, context, sortKey):
// registers handlerFn to be called whenever a message with ID msgId
// arrives on this dispatcher. `dispatcher` is a 256-entry table (one
// bucket per possible message-ID byte), each bucket a sorted linked list
// of registered handlers -- multiple handlers CAN coexist per ID. Every
// vanilla and custom message pair in this file goes through this same
// function. Confirmed __thiscall.
typedef void(__fastcall* AddHandler_t)(void*, int, u_int, void*, int, void*, int);
static AddHandler_t AddHandler = (AddHandler_t)0x00431620;

// SendMsg(connList, _, targetConnId, msgId, ...): sends a message to ONE
// specific connection. IMPORTANT: targetConnId has reserved sentinel
// values -- 0xFF means "broadcast to everyone", and any value whose low
// byte has the top bit set (>= 0x80) means "broadcast to everyone EXCEPT
// (targetConnId & 0x7F)". A real per-player connection ID must be read via
// TWO dereferences -- PlayerInfo+0x18 is a pointer to a connection OBJECT,
// and the actual small numeric ID SendMsg wants lives at that object's
// +0x3c. Passing PlayerInfo+0x18 directly (only one dereference) was the
// root cause of an intermittent, player-count-correlated crash: whenever
// that raw pointer's low byte happened to land in the 0x80-0xFF sentinel
// range, SendMsg would silently broadcast instead of unicast. Confirmed
// __thiscall.
typedef void(__fastcall* SendMsg_t)(void*, int, uint32_t, int, int, void*, int, int, char, char, int);
static SendMsg_t SendMsg = (SendMsg_t)0x0042f340;

// DestroySkater(skaterObj): tears down a skater object, same primitive
// vanilla EnterObserverMode uses when a player enters observer mode.
// Confirmed genuinely __stdcall (callee cleans its own stack).
typedef void(__stdcall* DestroySkater_t)(void*);
static DestroySkater_t DestroySkater = (DestroySkater_t)0x004f8f20;

// RemovePlayerReason(gameNetManager, _, player, reasonCode): removes a
// player from active play for a given reason (2 = "now observing", among
// others vanilla uses for kicks/timeouts/etc). This is the same call
// vanilla's own EnterObserverMode makes before destroying a skater.
// Confirmed __thiscall.
typedef void(__fastcall* RemovePlayerReason_t)(void* gameNetManager, int unused, void* player, int reason);
static RemovePlayerReason_t RemovePlayerReason = (RemovePlayerReason_t)0x0048a040;

/* ---- flag helpers: direct read/modify/write on PlayerInfo+0xf8 ---- */
static void SetFlags(void* player, uint32_t bits) { *(uint32_t*)((uint8_t*)player + PARTY_PLAYERINFO_FLAGS_OFFSET) |= bits; }
static void ClearFlags(void* player, uint32_t bits) { *(uint32_t*)((uint8_t*)player + PARTY_PLAYERINFO_FLAGS_OFFSET) &= ~bits; }

// ---- 1. client trigger: mirrors vanilla RequestObserverMode ----
// Sends the initial "let me leave observer mode" request to the host.
// If the caller IS the host, this is a loopback send to itself.
void __fastcall RequestExitObserverMode(int localPlayerSingleton) {
	printLog("RequestExitObserverMode: enter, localPlayerSingleton=%08X\n", localPlayerSingleton);
	void* localPlayer = GetLocalPlayer((void*)localPlayerSingleton);
	printLog("RequestExitObserverMode: localPlayer=%p\n", localPlayer);
	if (localPlayer != 0 && IsObserving_(localPlayer)) {
		printLog("RequestExitObserverMode: sending 0x7C\n");
		SendMsgToServer(*(void**)(localPlayerSingleton + 0x10), 0, MSG_ID_EXIT_OBSERVER_REQUEST, 0,
			(void*)0x0, 0x80, 2, '\b', '\0', 0);
		printLog("RequestExitObserverMode: send returned\n");
	}
}

// ---- 2. host-side request handler: mirrors vanilla 0047d2e0 ----
// Runs ONLY on the host, whenever a 0x7C request arrives (host's own
// loopback request included). Resolves who actually sent it, marks them
// pending on the host's authoritative PlayerInfo, and replies with 0x7D
// so the requester's own machine can proceed.
int __cdecl HandleExitObserverRequest(int msgCtx) {
	printLog("HandleExitObserverRequest: enter, msgCtx=%p\n", (void*)msgCtx);
	if (msgCtx == 0) {
		return 3;
	}
	void* gameNetManager = *(void**)(msgCtx + 0x4010);
	int connId = *(int*)(msgCtx + 0x4008);
	void* player = ResolvePlayer(gameNetManager, 0, connId);
	printLog("HandleExitObserverRequest: player=%p\n", player);
	if (player != 0 && IsObserving_(player)) {
		if (!IsLocalPlayer_(player)) {
			// Remote requester: set PENDING_PLAYER on the host's own
			// authoritative copy right now. (If the requester IS the
			// host, this is skipped -- their own flag gets set below,
			// in step 3, once their own 0x7D loopback arrives.)
			SetFlags(player, 8);
			// A non-host's own request doesn't otherwise force anyone
			// else observing; this QB script just gives the host time
			// before its OWN separate delayed LoadPendingPlayers call
			// (see host_process_remote_exit_observer in better4_menu.q).
			RunScript("host_process_remote_exit_observer", 0, 0, 0);
		}
		SendMsg(*(void**)((uint8_t*)gameNetManager + 0xc), 0,
			*(uint32_t*)(*(int*)(msgCtx + 0x4008) + 0x3c),
			MSG_ID_EXIT_OBSERVER_PROCEED, 0, (void*)0x0, 0x80, 0, '\0', '\0', 0);
		return 1;
	}
	return 3;
}

// Replicates the exact bookkeeping vanilla EnterObserverMode performs on a
// player entering observer mode -- remove-for-reason, destroy their
// skater, set OBSERVER (+ PENDING_PLAYER so they're also queued for
// LoadPendingPlayers later), and null the now-dangling skater pointer.
// We do this directly (rather than telling the target to run their own
// EnterObserverMode) because the host is the authoritative owner of every
// player's PlayerInfo, and this exact sequence is confirmed correct.
static void __fastcall ApplyObserverBookkeeping(void *gameNetManager, void* player) {
	void* oldSkater = *(void**)((uint8_t*)player + 0x14);
	RemovePlayerReason(gameNetManager, 0, player, 2);
	if (oldSkater != 0) {
		DestroySkater(oldSkater);
	}
	SetFlags(player, 4 | 8);   /* OBSERVER | PENDING_PLAYER */
	*(void**)((uint8_t*)player + 0x14) = 0;
}

// Walks the current player list and force-transitions every OTHER real,
// actively-playing (non-local, non-observing) player into observer mode.
// Only ever called by the HOST, and only as part of the host's own
// exit-observer flow (see ExitObserverModeLocal below) -- this is what
// satisfies vanilla LoadPendingPlayers' undocumented precondition that
// nobody else may still be actively skating when the HOST is the one
// being reconstructed.
static void __fastcall ForceAllOthersObserving(void *gameNetManager, void* self) {
	void* targets[16];
	int count = 0;

	struct { void* vtable; void* dummy; } searchCtx = { (void*)0x0058aa94, 0 };
	void* p = FirstPlayerInfo(gameNetManager, 0, &searchCtx, '\x01');
	while (p != 0 && count < 16) {
		printLog("ForceAllOthersObserving: scan found p=%p (self=%d local=%d observing=%d)\n",
			p, p == self, IsLocalPlayer_(p), IsObserving_(p));
		if (p != self && !IsLocalPlayer_(p) && !IsObserving_(p)) {
			targets[count++] = p;
		}
		p = NextPlayerInfo(&searchCtx);
	}
	printLog("ForceAllOthersObserving: found %d targets\n", count);

	for (int i = 0; i < count; i++) {
		ApplyObserverBookkeeping(gameNetManager, targets[i]);

		// See SendMsg_t's comment above: this MUST be two dereferences.
		// PlayerInfo+0x18 is a connection-object pointer, not the ID itself.
		void* connObjPtr = *(void**)((uint8_t*)targets[i] + 0x18);
		uint32_t connId = *(uint32_t*)((uint8_t*)connObjPtr + 0x3c);
		printLog("ForceAllOthersObserving: target=%p connId=%08X (low byte=%02X)\n",
			targets[i], connId, connId & 0xFF);

		// Send the REAL vanilla "proceed, enter observer mode" message
		// (0x4D) directly to this target's own connection -- their own
		// machine's existing, untouched 0047d600 handler runs genuine
		// EnterObserverMode() on itself in response. We never simulate
		// that transition ourselves.
		SendMsg(*(void**)((uint8_t*)gameNetManager + 0xc), 0,
			connId, 0x4D, 0, (void*)0x0, 0x80, 0, '\0', '\0', 0);
	}
}

// ---- 3. universal proceed handler: mirrors vanilla 0047d600 -> EnterObserverMode ----
// Runs on WHICHEVER machine receives 0x7D -- always exactly the original
// requester. Sets PENDING_PLAYER on that machine's own local player, and
// if that machine is the host, also kicks off ForceAllOthersObserving.
// LoadPendingPlayers itself is deliberately NOT called from here -- it's
// triggered by quit_observing.q's own QB-side Wait, after enough time has
// passed for every other client's real 0x4D-triggered transition to land.
void __fastcall ExitObserverModeLocal(void* gameNetManager) {
	void* player = GetLocalPlayer(gameNetManager);
	if (player != 0) {
		SetFlags(player, 8);
		if (IsHost()) {
			ForceAllOthersObserving(gameNetManager, player);
		}
	}
}

int __cdecl HandleExitObserverProceed(int msgCtx) {
	if (msgCtx == 0) {
		return 1;
	}
	ExitObserverModeLocal(*(void**)(msgCtx + 0x4010));
	return 1;
}

/* ---- 4. dispatcher registration wrappers ----
   These call the REAL thiscall functions (FUN_004869a0 / FUN_00486d80).
   __thiscall is not legal on a C function pointer/definition in MSVC's C
   compiler, so we fake it: __fastcall passes arg1 in ECX (== "this",
   matching thiscall) and arg2 in EDX, which the real thiscall callee
   never reads since it expects its real args pushed on the stack --
   the dummy 0 we pass for "unused" just occupies that ignored EDX slot,
   and every argument after it lands on the stack exactly where the real
   function expects it. */
typedef int(__fastcall* FUN_004869a0_t)(void*, int, char, char);
typedef void(__fastcall* FUN_00486d80_t)(void*, int, uint8_t, uint32_t, uint16_t, int);

static FUN_004869a0_t Real_FUN_004869a0 = (FUN_004869a0_t)0x004869a0;
static FUN_00486d80_t Real_FUN_00486d80 = (FUN_00486d80_t)0x00486d80;

// FUN_004869a0 is the HOST-ONLY dispatcher setup (creates the listening
// socket); this wrapper runs the real setup first, then additionally
// registers our 0x7C handler on it. Single call site in vanilla code.
int __fastcall FUN_004869a0_Wrapper(void* this, int unused, char param_1, char param_2) {
	int result = Real_FUN_004869a0(this, 0, param_1, param_2);
	printLog("FUN_004869a0_Wrapper: this=%p result=%d\n", this, result);
	if (result != 0) {
		void* dispatcher = (void*)(*(int*)((int)this + 0xc) + 0xc);
		printLog("FUN_004869a0_Wrapper: dispatcher=%p\n", dispatcher);
		AddHandler(dispatcher, 0, MSG_ID_EXIT_OBSERVER_REQUEST, HandleExitObserverRequest, 3, this, 0x80);
	}
	return result;
}

// FUN_00486d80 is the PER-CONNECTION dispatcher setup (one call site per
// local client slot); this wrapper additionally registers our 0x7D
// handler on each connection's dispatcher as it's set up.
void __fastcall FUN_00486d80_Wrapper(void* this, int unused, uint8_t param_1, uint32_t param_2, uint16_t param_3, int param_4) {
	Real_FUN_00486d80(this, 0, param_1, param_2, param_3, param_4);
	void* connPtr = *(void**)((int)this + param_4 * 4 + 0x10);
	if (connPtr != 0) {
		void* dispatcher = (void*)((int)connPtr + 0xc);
		printLog("FUN_00486d80_Wrapper: this=%p param_4=%d dispatcher=%p\n", this, param_4, dispatcher);
		AddHandler(dispatcher, 0, MSG_ID_EXIT_OBSERVER_PROCEED, HandleExitObserverProceed, 2, this, 0x80);
	}
}

// ---- 5. CFunc trigger the "Quit Observing" QB menu item calls ----
// Hijacks the dead-in-retail "DebugRenderIgnore" CFunc slot. Reads the
// local-player singleton and kicks off the whole request/reply chain.
int __cdecl CFunc_RequestExitObserverMode(CStruct *params) {
	printLog("CFunc_RequestExitObserverMode: enter\n");
	void* localPlayerSingleton = *(void**)PARTY_ADDR_LOCAL_PLAYER_SINGLETON;
	printLog("CFunc_RequestExitObserverMode: single read\n");
	if (localPlayerSingleton != 0) {
		RequestExitObserverMode((int)localPlayerSingleton);
	}
	printLog("CFunc_RequestExitObserverMode: returned from RequestExitObserverMode\n");
	return 1;
}

// Fixes a real vanilla bug: LoadPendingPlayers hardcodes a promoted
// player's new flags to just JUMPING_IN, silently dropping LOCAL_PLAYER
// if it was set. This preserves it instead.
_declspec(naked) void fixJumpingInFlags() {
	__asm {
		push eax
		mov eax, [esi + 0xF8]   // original player's m_flags
		and eax, 1              // isolate mLOCAL_PLAYER (bit 0)
		or eax, 0x10            // combine with mJUMPING_IN
		mov[esp + 0x44], eax    // write into new_player.Flags slot
		pop eax
		ret
	}
}

void patchObserverRejoinFlags() {
	// replace MOV [ESP+0x3C], 0x10 (8 bytes) with a CALL (5 bytes) + 3 NOPs
	patchCall((void*)0x0048b982, fixJumpingInFlags);
	patchNop((void*)(0x0048b982 + 5), 3);
}

_declspec(naked) void skipNowObservingMsg() {
	__asm {
		cmp ebx, 2
		je skip_toast
		mov eax, 0x0048c650
		jmp eax              // not reason 2 -- tail-call the real toast function unchanged
	skip_toast :
		ret 0x20              // pops the return address, THEN cleans the 8 stack args -- exactly what FUN_0048c650's own RET 0x20 would have done
	}
}
static int g_ourPendingPlayersCall = 0;

// Toggled (not set/cleared separately) by QB, bracketing our own
// LoadPendingPlayers calls. Hijacks the dead "debugrendermode" CFunc slot
// (confirmed unused/safe earlier this session).
int __cdecl CFunc_ToggleOurPendingPlayersFlag(CStruct *params) {
	g_ourPendingPlayersCall = !g_ourPendingPlayersCall;
	return 1;
}
void patchSkipNowObservingMsg(void) {
	patchCall((void*)0x0048a29f, skipNowObservingMsg);
}

_declspec(naked) void skipObserverJoinMsg() {
	__asm {
		cmp dword ptr[g_ourPendingPlayersCall], 0
		jne skip_toast          // it IS our own call -- suppress "joining"
		mov eax, 0x0048c650
		jmp eax                 // genuine natural trigger -- show it normally
		skip_toast :
		ret 0x20
	}
}

void patchSkipObserverJoinMsg(void) {
	patchCall((void*)0x0048822c, skipObserverJoinMsg);
}

typedef int(__fastcall* OthersRemainingCount_t)(int gameNetManager);
static OthersRemainingCount_t OthersRemainingCount = (OthersRemainingCount_t)0x00489d90;

typedef void(__fastcall* FUN_0048be30_t)(void*);
static FUN_0048be30_t Real_FUN_0048be30 = (FUN_0048be30_t)0x0048be30;

int __cdecl CFunc_EnterObserverModePending(CStruct* params) {
	__try {
		void* gameNetManager = *(void**)PARTY_ADDR_GAMENET_MANAGER;
		void* self = GetLocalPlayer(gameNetManager);
		printLog("CFunc_EnterObserverModePending: self=%p\n", self);
		if (self != 0 && !IsObserving_(self)) {
			ApplyObserverBookkeeping(gameNetManager, self);
			printLog("CFunc_EnterObserverModePending: applied\n");
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		printLog("CFunc_EnterObserverModePending: caught exception, skipping this tick\n");
	}
	return 1;
}
_declspec(naked) void hookWaitForPlayersDialog() {
	__asm {
		mov eax, 0x00413420
		call eax              // replicate the original CALL FUN_00413420(...) exactly, args already pushed
		pushad
		push 0
		call CFunc_EnterObserverModePending
		add esp, 4
		popad
		ret                   // plain ret -- original caller's own ADD ESP,0x10 handles cleanup after this returns
	}
}

void patchAutoEnterObserverOnRunEnded(void) {
	patchCall((void*)0x00484235, hookWaitForPlayersDialog);
}

void patchSerialCheck(void) {
	patchJmp((void*)0x0048224e, (void*)0x00482394);
	patchNop((void*)(0x0048224e + 5), 1);
}

void initExitObserverPatches(void) {
	patchCall((void*)0x00500a26, FUN_004869a0_Wrapper);
	patchCall((void*)0x0050d7b8, FUN_00486d80_Wrapper);
	patchCall((void*)0x0050d85b, FUN_00486d80_Wrapper);
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
	addCFunc("RequestExitObserverMode", (void *)CFunc_RequestExitObserverMode);
	addCFunc("ToggleOurPendingPlayersFlag", (void *)CFunc_ToggleOurPendingPlayersFlag);
	addCFunc("GetIniBool", (void *)CFunc_GetIniBool);
	addCFunc("GetIniInteger", (void *)CFunc_GetIniInteger);
	addCFunc("SetIniBool", (void *)CFunc_SetIniBool);
	addCFunc("SetIniInteger", (void *)CFunc_SetIniInteger);
	addCFunc("ChangeGlobal", (void *)CFunc_ChangeGlobal);
	addCFunc("SetSpinKeysControl", (void *)CFunc_SetSpinKeysControl);
	addCFunc("SetSpineTransferControl", (void *)CFunc_SetSpineTransferControl);
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

			// New patches
			patchObserverRejoinFlags();
			initExitObserverPatches();
			patchSkipNowObservingMsg();
			patchSkipObserverJoinMsg();
			patchSerialCheck();
			//patchAutoEnterObserverOnRunEnded();
			//
		
			//patchSkipSkaterDestroy();
		
			//patchByte((void*)0x488FAB, 12); 
			//patchJmpTest();

			//patchPrintf();
			patchScriptPrintf();
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
