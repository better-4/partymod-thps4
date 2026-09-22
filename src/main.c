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
#include <patch.h>
#include <script.h>

#define VERSION_NUMBER_MAJOR 1
#define VERSION_NUMBER_MINOR 0
#define VERSION_NUMBER_FIX 11


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

	printf("ORIG: %f CORRECTED: %f FRAMETIME: %f UNK: %f FRICTION: %f LENGTH: %f CALCFRICTION: %f\n", origFriction, correctedFriction, frametime, unk, friction, length, calcFriction);*/

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

	printf("Patched online server: %s\n", domainStr);
}

char configFile[1024];

void initPatch() {
	GetModuleFileName(NULL, &executableDirectory, filePathBufLen);

	// find last slash
	char *exe = strrchr(executableDirectory, '\\');
	if (exe) {
		*(exe + 1) = '\0';
	}

	sprintf(configFile, "%s%s", executableDirectory, CONFIG_FILE_NAME);

	printf("PARTYMOD for THPS4 %d.%d.%d\n", VERSION_NUMBER_MAJOR, VERSION_NUMBER_MINOR, VERSION_NUMBER_FIX);
	printf("DIRECTORY: %s\n", executableDirectory);

	//patchResolution();

	initScriptPatches();

	/*int disableMovies = getIniBool("Miscellaneous", "NoMovie", 0, configFile);
	if (disableMovies) {
		printf("Disabling movies\n");
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

	printf("Patch Initialized\n");
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
	// printf("CPendingTricks::CPendingTricks\n");

	*pending_tricks = malloc(sizeof(struct FixedPendingTricks));

	(*pending_tricks)->trick_count = 0;
}

uint8_t __fastcall CPendingTricks_FlushTricks_Wrapper(struct FixedPendingTricks **pending_tricks) {
	uint8_t (__fastcall * CPendingTricks_FlushTricks)(struct FixedPendingTricks *) = (void *)0x004e0f90;
	// printf("CPendingTricks::FlushTricks\n");

	(*pending_tricks)->trick_count = 0;

	return 1;
}

uint32_t lastcount = 0;

uint32_t __fastcall CPendingTricks_TrickOffObject_Wrapper(struct FixedPendingTricks **pending_tricks, void *pad, uint32_t obj) {
	uint32_t (__fastcall * CPendingTricks_TrickOffObject)(struct FixedPendingTricks *, void *, uint32_t) = (void *)0x004e0dd0;
	// printf("CPendingTricks::TrickOffObject: trick_count=%d\n", (*pending_tricks)->trick_count);

	if ((*pending_tricks)->trick_count > MAX_PENDING_TRICKS) {
		(*pending_tricks)->trick_count = MAX_PENDING_TRICKS;
	}

	return CPendingTricks_TrickOffObject(*pending_tricks, pad, obj);
}

uint32_t __fastcall CPendingTricks_WriteToBuffer_Wrapper(struct FixedPendingTricks **pending_tricks, void *pad, uint32_t *buf, uint32_t size) {
	uint32_t (__fastcall * CPendingTricks_WriteToBuffer)(struct FixedPendingTricks *, void *, uint32_t *, uint32_t) = (void *)0x004e0e90;
	printf("CPendingTricks::WriteToBuffer: trick_count=%d\n", (*pending_tricks)->trick_count);

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

	printf("CGoalManager::Land_Graffiti: trick_count=%d, last_score_landed=%d\n", (*pending_tricks)->trick_count, last_score_landed);

	for (int i = 0; i < (*pending_tricks)->trick_count; i++) {
		printf("CGoalManager::Land_Graffiti: Got trick object %x\n", (*pending_tricks)->checksums[i]);
		CGoalManager_GotTrickObject(goal_manager, NULL, (*pending_tricks)->checksums[i], last_score_landed);
	}
}

void __fastcall Score_LogTrickObject_Wrapper(void *pScore, void *pad, uint32_t skater_id, uint32_t score, uint32_t trick_count, uint32_t* pending_tricks, uint8_t propagate) {
	void (__fastcall * Score_LogTrickObject)(void *, void *, uint32_t, uint32_t, uint32_t, struct FixedPendingTricks**, uint8_t) = (void*)0x004f70d0;
	printf("Score::LogTrickObject: skater_id=%d trick_count=%d score=%d\n", skater_id, trick_count, score);

	Score_LogTrickObject(pScore, pad, skater_id, score, trick_count, pending_tricks, propagate);
}

// Registered network message handler for opcode 0x3b (the LogTrickObjectRequest
// message a client sends to the host). Invoked via a stored function pointer
// (Mdl::Skate::AddNetworkMsgHandlers), not a direct call site, so we intercept
// it by overwriting the function pointer immediate at both registration sites
// instead of using patchCall.
//
// This exists to test whether incoming trick-object messages are being
// silently rejected by the sender/receiver sequence-byte check at the top of
// the original handler (msg[0] must be 1, msg[1] must match the receiver's
// current GameNet sequence byte) -- a check that has nothing to do with the
// tag limit patch's trick count, which would explain drops independent of
// combo size.
uint32_t Score_LogTrickObjectReceive_Wrapper(char *msg) {
	uint32_t (*Score_LogTrickObjectReceive)(char *) = (void *)0x004b5810;
	uint8_t (__fastcall * GameNet_GetLocalSeq)(void *, void *) = (void *)0x0048c360;
	void **GameNet_Manager_Instance = (void **)0x00ab5394;

	uint8_t flag = msg[0];
	uint8_t sender_seq = msg[1];
	uint8_t local_seq = GameNet_GetLocalSeq(*GameNet_Manager_Instance, NULL);
	uint32_t skater_id = *(uint32_t *)(msg + 4);
	uint32_t score = *(uint32_t *)(msg + 8);
	uint32_t trick_count = *(uint32_t *)(msg + 0xc);

	printf("[t=%u] LogTrickObject RECV: flag=%d sender_seq=%d local_seq=%d skater_id=%d score=%d trick_count=%d -> %s\n",
		GetTickCount(), flag, sender_seq, local_seq, skater_id, score, trick_count,
		(flag == 1 && sender_seq == local_seq) ? "ACCEPTED" : "REJECTED (seq mismatch)");

	return Score_LogTrickObjectReceive(msg);
}

// Only 1 in ~6-7 landed combos actually reached the receive handler above,
// and the one that did was accepted cleanly (sequence bytes matched) -- so
// the drop isn't happening at the receive-side gate, it's happening before
// the message ever leaves.
//
// NOTE: Ghidra's decompiler gives WRONG pseudocode for FUN_004301f0 (it
// hallucinates a single-pointer check at this+0x1f68). The real disassembly
// (confirmed against the function's actual body_start/body_end) shows it
// walks a linked list rooted at a sentinel node at this+0x1f5c, taking the
// first real entry after the sentinel, and skips the call to FUN_0042f340
// entirely (jumps straight to the epilogue, no log, no error) if that walk
// doesn't find a usable entry. This wrapper just logs the send attempt
// itself; see LogTrickObjectSend_Inner_Wrapper below for the actual gate.
void __fastcall LogTrickObjectRequest_Send_Wrapper(void *dest, void *pad, char msgType, uint32_t size, void *buf, int param_4, int param_5, char param_6, char param_7, int param_8) {
	void (__fastcall * orig)(void *, void *, char, uint32_t, void *, int, int, char, char, int) = (void *)0x004301f0;

	printf("[t=%u] LogTrickObjectRequest SEND: dest=%p size=%d (attempting)\n", GetTickCount(), dest, size);

	orig(dest, pad, msgType, size, buf, param_4, param_5, param_6, param_7, param_8);
}

// Wraps the CALL 0x0042f340 instruction at 0x004302ae, inside FUN_004301f0's
// body. This is only reached if FUN_004301f0's internal list-walk (see note
// above) actually finds a usable entry -- if messages are being dropped by
// that gate, this log line simply won't appear for a given send attempt,
// even though LogTrickObjectRequest_Send_Wrapper's "(attempting)" line did.
// Filtered to opcode 0x3b (';') only, since FUN_004301f0/FUN_0042f340 are
// generic send functions used by many unrelated message types.
// 0x0042fefe/0x0042ff10 (see below) turned out to be a shared code path used
// by many unrelated message types going through this same branch, so their
// logs were mostly noise from other game traffic, not our trick messages.
// This flag scopes those two hooks to only fire while we're inside our own
// synchronous call into FUN_0042f340 (safe: it doesn't recurse/reenter).
static int g_watchingTrickSend = 0;

void __fastcall LogTrickObjectSend_Inner_Wrapper(void *dest, void *pad, uint32_t routeId, char msgType, uint32_t size, void *buf, int param_5, int param_6, char param_7, char param_8, int param_9) {
	void (__fastcall * orig)(void *, void *, uint32_t, char, uint32_t, void *, int, int, char, char, int) = (void *)0x0042f340;
	int isTrickMsg = (msgType == 0x3b);

	if (isTrickMsg) {
		printf("LogTrickObjectRequest SEND: reached FUN_0042f340, dest=%p routeId=%u size=%d branch=%d\n",
			dest, routeId, size, param_6);
		g_watchingTrickSend = 1;
	}

	orig(dest, pad, routeId, msgType, size, buf, param_5, param_6, param_7, param_8, param_9);

	g_watchingTrickSend = 0;
}

// FUN_0042f340 is reached on every attempt, but only ~1 in 3 messages
// actually arrives. Traced the real disassembly (not the decompiler, which
// led me to the wrong addresses once already -- param_6==2 is actually the
// FIRST branch checked, falling through at 0x0042f3d0, not the last one).
// The single-recipient lookup for our routeId (param_1, not 0xff/negative)
// searches the list at this+0x1f5c for an entry matching routeId, then at
// 0x0042f708/0x0042f70b checks `*(byte*)(entry+0x5c) & 8`: if that bit is
// CLEAR it falls through to build+enqueue the message (CALL 0x00430c80 at
// 0x0042f7ab); if SET it jumps straight past that to the discard check
// (CALL 0x004341d0 at 0x0042f7c3). These wrap those two real call sites,
// gated by g_watchingTrickSend since both sites are shared by other message
// types going through the same branch.
void __fastcall TrickSend_Enqueued_Wrapper(void *node, void *pad, int param_1) {
	void (__fastcall * orig)(void *, void *, int) = (void *)0x00430c80;
	if (g_watchingTrickSend) {
		printf("LogTrickObjectRequest SEND: FUN_00430c80 -> recipient ready, message WILL be sent\n");
	}
	orig(node, pad, param_1);
}

void __fastcall TrickSend_Discarded_Wrapper(int msgObj) {
	void (__fastcall * orig)(int) = (void *)0x004341d0;
	if (g_watchingTrickSend) {
		printf("LogTrickObjectRequest SEND: FUN_004341d0 -> no ready recipient found, message DISCARDED\n");
	}
	orig(msgObj);
}

// Cross-referenced against the Mac debug build (Net::App::handle_sequenced_messages):
// FUN_004324a0 is the low-level receive-side gate for ALL "sequenced" channel
// messages (registered for opcode 7 via Net::Dispatcher::AddHandler at
// 0x0042d3e3, inside FUN_0042d2b0 -- the same core Net::App init that opens
// the UDP socket). Every sequenced message, regardless of its real game-level
// type, passes through here first. Wire format: byte 0 = channel, bytes 1-4 =
// sequence number. Logic (confirmed matching the Mac source):
//   - if incoming seq < *(expected_seq[channel]): stale, ignored, return 1
//   - else: walk the channel's pending list looking for insertion point;
//     if an entry with the SAME seq already exists, this exact message is
//     treated as a duplicate and discarded (this is normal/expected --  it's
//     how a legitimate resend of an already-queued-but-undispatched message
//     gets deduped, not evidence of a bug on its own)
//   - otherwise inserted in sorted position, to be drained later once
//     expected_seq[channel] catches up to it
//
// This wraps that gate directly (channel byte 8 = the channel our trick
// messages use, per FUN_0042f340's per-channel counter at entry+0x74+8*4)
// to see, for every packet that reaches this point at all: the incoming
// sequence number, what the receiver currently expects, and the outcome.
// Since client and server are the same machine (loopback), genuine packet
// loss should be near-impossible -- if trick-log messages we know were
// "successfully queued" client-side (see TrickSend_Enqueued_Wrapper) never
// show up here at all, that proves the loss happens between the send queue
// and the socket, not in this receive-side logic. If they DO show up here
// but get treated as stale/duplicate unexpectedly, that points at a sequence
// bookkeeping bug instead.
void __cdecl SequencedMsgGate_Wrapper(char *msg) {
	uint32_t (__cdecl * orig)(char *) = (void *)0x004324a0;
	uint8_t channel = msg[0];

	if (channel == 8) {
		uint32_t incoming_seq = *(uint32_t *)(msg + 1);
		char *expected_base = *(char **)(msg + 0x4008);
		uint32_t expected_seq = *(uint32_t *)(expected_base + 0x474 + channel * 4);
		uint8_t submsg_type = (uint8_t)msg[5];

		printf("SequencedMsgGate: channel=8 submsg_type=0x%02x incoming_seq=%u expected_seq=%u -> %s\n",
			submsg_type, incoming_seq, expected_seq,
			(incoming_seq < expected_seq) ? "STALE (ignored)" : "in-range (queued or duplicate, see next log)");
	}

	orig(msg);
}

// Net::App::process_sequenced_messages (0x0042d770) walks up to 256 channels
// each tick, draining each channel's queue while the front message's sequence
// number matches what's expected. Critically: if DispatchMessage returns
// anything other than 1 for ANY channel, the function returns IMMEDIATELY --
// every channel after that one (including our channel 8) gets skipped
// entirely for this tick, even if messages on channel 8 are fully in-order
// and ready (which SequencedMsgGate already confirmed they are). This wraps
// DispatchMessage's call site specifically inside process_sequenced_messages
// (0x0042d8e8, not its other two call sites elsewhere) to log the message
// type and return code for every channel drained each tick, so we can catch
// an early abort happening before channel 8 gets its turn.
int __fastcall ProcessSequencedDispatch_Wrapper(void *dispatcher, void *pad, char *buf) {
	int (__fastcall * orig)(void *, void *, char *) = (void *)0x00431a80;
	uint8_t msgtype = (uint8_t)buf[0x4000];
	int result = orig(dispatcher, pad, buf);

	if (result != 1) {
		printf("process_sequenced_messages: DispatchMessage(msgtype=0x%02x) returned %d -> ABORTING remaining channels this tick!\n",
			msgtype, result);
	} else if (msgtype == 0x3b) {
		printf("process_sequenced_messages: DispatchMessage(msgtype=0x3b) -> dispatched normally\n");
	}

	return result;
}

// ROOT CAUSE: Net::Dispatcher::DispatchMessage's handler search requires
// (candidate_flags & msg_flags) == msg_flags before it'll actually call a
// registered handler -- a flags-subset match, not just an opcode match.
// Our opcode-0x3b handler is registered twice: once from Init with a
// register-sourced (effectively arbitrary at that point in startup) flags
// value, and once from Mdl::Skate::AddNetworkMsgHandlers with a FIXED flags
// value of 0 (confirmed via disassembly: "PUSH 0x0" immediately before the
// opcode/handler/priority pushes at 0x00500f9e). A candidate registered with
// flags=0 only matches messages whose own flags happen to also be exactly 0
// -- everything else silently skips it, and if no other candidate matches
// either, DispatchMessage just returns 1 ("no handler matched") without ever
// calling us. This exactly matches what we measured: 11 separate
// "dispatched normally" calls for msgtype=0x3b, but only 1 actual
// LogTrickObject RECV.
//
// Fix: intercept both AddHandler call sites (can't safely patch the pushed
// flags value directly -- one of them pushes a register, not an immediate,
// so overwriting bytes in place isn't safe) and force flags to 0xFFFFFFFF
// specifically for opcode 0x3b, so it matches every possible msg_flags value.
void *__fastcall AddHandler_FixTrickObjectFlags_Wrapper(void *dispatcher, void *pad, uint32_t opcode, void *handlerFn, uint32_t flags, void *context, uint32_t priority) {
	void *(__fastcall * orig)(void *, void *, uint32_t, void *, uint32_t, void *, uint32_t) = (void *)0x00431620;

	if (opcode == 0x3b) {
		printf("AddHandler: opcode 0x3b registered with flags=0x%x, forcing to 0xffffffff\n", flags);
		flags = 0xffffffff;
	}

	return orig(dispatcher, pad, opcode, handlerFn, flags, context, priority);
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
	// NOTE: 0x004f70a9 used to be patched here too, under the same "fix size of
	// msg sent to server" assumption. It's NOT a size -- it's forwarded through
	// FUN_004301f0 -> FUN_0042f340 and stored as *(int*)(queue_node+8), the
	// sort key FUN_0042f340/FUN_00430c80 use to order this message in the
	// destination's outbound delivery queue (lower = sent sooner). Scaling it
	// from 128 to 2048 made every trick-log message look like the lowest
	// priority thing on the wire, so it only got sent whenever the queue
	// happened to be otherwise empty -- confirmed by instrumentation: every
	// attempt reached FUN_00430c80 (successfully queued), but only a small
	// fraction were ever actually received. Leaving this constant alone fixes
	// delivery; it never needed to scale with MAX_PENDING_TRICKS.
	
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

	// DEBUG: instrument the receive-side handler for the LogTrickObjectRequest
	// message (opcode 0x3b) by redirecting both of its registration sites
	// (Mdl::Skate::AddNetworkMsgHandlers, called once from Init and once from
	// FUN_00500cd0) to log the sequence-byte check before forwarding to the
	// original handler.
	patchDWord(0x004cd22e + 1, Score_LogTrickObjectReceive_Wrapper);
	patchDWord(0x00500fa0 + 1, Score_LogTrickObjectReceive_Wrapper);

	// DEBUG: instrument LogTrickObjectRequest's call into the low-level send
	// function to check whether messages are being silently dropped before
	// they ever leave the client (see wrapper comment above).
	patchCall(0x004f70b2, LogTrickObjectRequest_Send_Wrapper);

	// DEBUG: instrument FUN_004301f0's internal call into FUN_0042f340, which
	// only happens if its internal list-walk gate passes. Comparing this
	// against the "(attempting)" log above tells us whether messages are
	// being silently gated out inside FUN_004301f0 itself.
	patchCall(0x004302ae, LogTrickObjectSend_Inner_Wrapper);

	// DEBUG: instrument the two outcomes of FUN_0042f340's single-recipient
	// lookup branch (see wrapper comments above) to see whether messages are
	// being discarded because no ready recipient was found.
	patchCall(0x0042f7ab, TrickSend_Enqueued_Wrapper);
	patchCall(0x0042f7c3, TrickSend_Discarded_Wrapper);

	// DEBUG: instrument the actual receive-side sequencing gate (see wrapper
	// comment above), registered via a stored function pointer (not a direct
	// call site) at 0x0042d3e3 inside FUN_0042d2b0's core Net::App init.
	patchDWord(0x0042d3e3 + 1, SequencedMsgGate_Wrapper);

	// DEBUG: instrument the DispatchMessage call site inside
	// Net::App::process_sequenced_messages specifically (see wrapper comment
	// above) to catch the 256-channel drain loop aborting early, before
	// channel 8 (our trick messages) gets a turn.
	patchCall(0x0042d8e8, ProcessSequencedDispatch_Wrapper);

	// FIX: force the opcode-0x3b handler registration's flags to 0xffffffff
	// at both call sites (Init and Mdl::Skate::AddNetworkMsgHandlers) so
	// DispatchMessage's flags-subset check always matches (see wrapper
	// comment above for the root cause this addresses).
	patchCall(0x004cd237, AddHandler_FixTrickObjectFlags_Wrapper);
	patchCall(0x00500fa9, AddHandler_FixTrickObjectFlags_Wrapper);
}

/*
	End Tag Limit Patch
*/

void partyMain() {
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
	//patchSkipSkaterDestroy();
	
	//patchByte((void*)0x488FAB, 12); 
	//patchJmpTest();

	//patchPrintf();
	//patchCD();
}
