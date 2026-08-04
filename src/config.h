#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <SDL2/SDL.h>

#define CONFIG_FILE_NAME "partymod.ini"

struct keybinds {
	SDL_Scancode menu;
	SDL_Scancode cameraToggle;
	SDL_Scancode cameraSwivelLock;

	SDL_Scancode grind;
	SDL_Scancode grab;
	SDL_Scancode ollie;
	SDL_Scancode kick;

	SDL_Scancode leftSpin;
	SDL_Scancode rightSpin;
	SDL_Scancode nollie;
	SDL_Scancode switchRevert;

	SDL_Scancode item_up;
	SDL_Scancode item_down;
	SDL_Scancode item_left;
	SDL_Scancode item_right;

	SDL_Scancode right;
	SDL_Scancode left;
	SDL_Scancode up;
	SDL_Scancode down;

	SDL_Scancode cameraRight;
	SDL_Scancode cameraLeft;
	SDL_Scancode cameraUp;
	SDL_Scancode cameraDown;
};

// a recreation of the SDL_GameControllerButton enum, but with the addition of right/left trigger
typedef enum {
	CONTROLLER_UNBOUND = -1,
	CONTROLLER_BUTTON_A = SDL_CONTROLLER_BUTTON_A,
	CONTROLLER_BUTTON_B = SDL_CONTROLLER_BUTTON_B,
	CONTROLLER_BUTTON_X = SDL_CONTROLLER_BUTTON_X,
	CONTROLLER_BUTTON_Y = SDL_CONTROLLER_BUTTON_Y,
	CONTROLLER_BUTTON_BACK = SDL_CONTROLLER_BUTTON_BACK,
	CONTROLLER_BUTTON_GUIDE = SDL_CONTROLLER_BUTTON_GUIDE,
	CONTROLLER_BUTTON_START = SDL_CONTROLLER_BUTTON_START,
	CONTROLLER_BUTTON_LEFTSTICK = SDL_CONTROLLER_BUTTON_LEFTSTICK,
	CONTROLLER_BUTTON_RIGHTSTICK = SDL_CONTROLLER_BUTTON_RIGHTSTICK,
	CONTROLLER_BUTTON_LEFTSHOULDER = SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
	CONTROLLER_BUTTON_RIGHTSHOULDER = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
	CONTROLLER_BUTTON_DPAD_UP = SDL_CONTROLLER_BUTTON_DPAD_UP,
	CONTROLLER_BUTTON_DPAD_DOWN = SDL_CONTROLLER_BUTTON_DPAD_DOWN,
	CONTROLLER_BUTTON_DPAD_LEFT = SDL_CONTROLLER_BUTTON_DPAD_LEFT,
	CONTROLLER_BUTTON_DPAD_RIGHT = SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
	CONTROLLER_BUTTON_MISC1 = SDL_CONTROLLER_BUTTON_MISC1,
	CONTROLLER_BUTTON_PADDLE1 = SDL_CONTROLLER_BUTTON_PADDLE1,
	CONTROLLER_BUTTON_PADDLE2 = SDL_CONTROLLER_BUTTON_PADDLE2,
	CONTROLLER_BUTTON_PADDLE3 = SDL_CONTROLLER_BUTTON_PADDLE3,
	CONTROLLER_BUTTON_PADDLE4 = SDL_CONTROLLER_BUTTON_PADDLE4,
	CONTROLLER_BUTTON_TOUCHPAD = SDL_CONTROLLER_BUTTON_TOUCHPAD,
	CONTROLLER_BUTTON_RIGHTTRIGGER = 21,
	CONTROLLER_BUTTON_LEFTTRIGGER = 22,
} controllerButton;

typedef enum {
	CONTROLLER_STICK_UNBOUND = -1,
	CONTROLLER_STICK_LEFT = 0,
	CONTROLLER_STICK_RIGHT = 1
} controllerStick;

struct controllerbinds {
	controllerButton menu;
	controllerButton cameraToggle;
	controllerButton cameraSwivelLock;

	controllerButton grind;
	controllerButton grab;
	controllerButton ollie;
	controllerButton kick;

	controllerButton leftSpin;
	controllerButton rightSpin;
	controllerButton nollie;
	controllerButton switchRevert;

	controllerButton right;
	controllerButton left;
	controllerButton up;
	controllerButton down;

	controllerStick movement;
	controllerStick camera;
};

// ============================================================================
// Custom bind implementation overview (wip)
//
//   1. spineButtonMode   - used native code hook that partymod already uses, see
//                          checkSpineTransferButtons() in input.c
// 
//   2. dropdownButtonMode - uses bps patches to patch qb trigger on launch, see 
//							 registerInputScriptPatches() in script.c and the 
//							 GrindRelease patches under patches
// 
//   3. spinButtonMode    - performs remapping of actual controller binds (native hook
//							not found yet). this makes dropdown modes 4-7 not work if 
//							spin mode is not 0 (vanilla). see loadControllerBinds() in config.c)
//							(also happens changing binds manually)
// ============================================================================

// SPINE TRANSFER button combo (checked natively via comp+0x834 (R2) / comp+0x87c
// (L2) for the L2/R2 modes, or dedicated physicalL1Held/physicalR1Held globals
// - updated every frame straight from SDL, bypassing all rebinding - for the
// L1/R1 modes). See checkSpineTransferButtons() in input.c for the actual logic.
typedef enum {
	SPINE_MODE_EITHER = 0,	// L2 or R2 (default, matches original PS2/PC behavior)
	SPINE_MODE_L2_ONLY = 1,
	SPINE_MODE_R2_ONLY = 2,
	SPINE_MODE_BOTH = 3,	// L2 and R2 together
	SPINE_MODE_L1_ONLY = 4,	// only L1 triggers spine transfer
	SPINE_MODE_R1_ONLY = 5,	// only R1 triggers spine transfer
	SPINE_MODE_L1R1_BOTH = 6,	// requires holding L1+R1 together
	SPINE_MODE_L1R1_EITHER = 7	// L1 or R1
} spineButtonMode;

// GRIND DROPDOWN button combo (leaving a rail/grind without an ollie - the
// SkateInOrBail script invoked by the GrindRelease Trigger table in
// scripts\grindscripts.qb). Implemented as compiled-data BPS patches, NOT
// native code - see registerInputScriptPatches() in script.c for which patch
// blob gets selected for each value. IMPORTANT: only takes effect when
// UsePS2Controls=1 - the non-PS2-controls patch (grindscripts_pcadddd.bps)
// restructures GrindRelease for an unrelated historical purpose and doesn't
// support per-mode selection.
typedef enum {
	DROPDOWN_MODE_EITHER = 0,	// L2 or R2 (default, matches PARTYMOD/original behavior)
	DROPDOWN_MODE_L2_ONLY = 1,	// only L2 works; always drops left
	DROPDOWN_MODE_R2_ONLY = 2,	// only R2 works; always drops right
	DROPDOWN_MODE_BOTH = 3,		// requires holding L2+R2 together; always drops right
	DROPDOWN_MODE_L1R1_EITHER = 4,	// L1 or R1; L1 drops left, R1 drops right
	DROPDOWN_MODE_L1_ONLY = 5,	// only L1 works; always drops left
	DROPDOWN_MODE_R1_ONLY = 6,	// only R1 works; always drops right
	DROPDOWN_MODE_L1R1_BOTH = 7	// requires holding L1+R1 together; always drops right
} dropdownButtonMode;


// IMPORTANT - how this interacts with DropdownButtonMode:
//     DropdownButtonMode 0-3 (L2/R2-based) work correctly with ANY SpinButtonMode
//     value - they key off Nollie/Switch's binding, which this enum never
//     touches.
// 
//     DropdownButtonMode 4-7 (L1/R1-based) ONLY work correctly when
//     SpinButtonMode = SPIN_MODE_L1_R1 (0, vanilla). This isn't a bug: the
//     GrindRelease Trigger table's "L1"/"R1" checks can only ever reflect
//     whatever leftSpin/rightSpin currently write into controlData[16]/[17]
typedef enum {
	SPIN_MODE_L1_R1 = 0,	// vanilla: L1 = left, R1 = right
	SPIN_MODE_L2_R2 = 1,	// L2 = left, R2 = right
	SPIN_MODE_R1_R2 = 2,	// R1 = right, R2 = left
	SPIN_MODE_L1_L2 = 3	// L1 = right, L2 = left
} spinButtonMode;

struct inputsettings {
	uint8_t isPs2Controls;
	uint8_t dropdownEnabled;
	uint8_t useKeyboardControls;
	spineButtonMode spineTransferMode;
	dropdownButtonMode dropdownMode;	
	// (note: spinButtonMode is NOT stored here - it's read directly inside
	// loadControllerBinds() in config.c since it only affects controllerbinds,
	// not inputsettings)
};

void loadInputSettings(struct inputsettings* settingsOut);
void loadControllerBinds(struct controllerbinds* bindsOut);
void loadKeyBinds(struct keybinds* bindsOut);
int getIniBool(char* section, char* key, int def, char* file);
void getOptimalRefreshRate(uint32_t* freq, uint32_t* interval);

void patchLoadConfig();
void dumpSettings();
void patchWindow();
void patchLoadConfig();

#endif
