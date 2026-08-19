#ifndef _INPUT_H_
#define _INPUT_H_

typedef struct {
	uint32_t vtablePtr;
	//uint32_t node;
	uint32_t type;
	uint32_t port;
	// 16
	uint32_t slot;
	uint32_t isValid;
	uint32_t unk_24;
	uint8_t controlData[32];	// PS2 format control data
	uint8_t vibrationData_align[32];
	uint8_t vibrationData_direct[32];
	uint8_t vibrationData_max[32];
	uint8_t vibrationData_oldDirect[32];    // there may be something before this
	// 160
	// 176
	//uint32_t unk4;
	//uint32_t unk4;
	//uint32_t unk4;
	uint8_t unkChunk[100];
	uint32_t unk5;
	// 192
	uint32_t unk6;
	uint32_t actuatorsDisabled;	// +0x124
	uint32_t capabilities;	// +0x128
	uint32_t unk7;	// +0x12c
	// 208
	uint32_t num_actuators; // +0x130
	uint32_t unk8;	// +0x134
	uint32_t unk9;	// +0x138
	uint32_t state;	// +0x13c
	uint32_t test;	// +0x140
	// 224
	uint32_t index;	// CORRECT HERE!!	+0x144
	uint32_t isPluggedIn;	// +0x148
	uint32_t unplugged_counter;
	uint32_t unplugged_retry;

	uint32_t pressed;
	uint32_t start_or_a_pressed;
	// 238
} device;

int processIntroEvent();
void patchInput();
void __cdecl processController(device *dev);

#endif
