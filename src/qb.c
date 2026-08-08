#include "qb.h"
#define UNUSED 0

typedef uint32_t unused_t;

// A CStruct in THPS4 is a linked list of components, which are akin to key-value pairs.
// Components are identified by checksum, representing the CRC32 of the component's name.
// They also contain a type (int, str, CStruct, etc.) and associated value.

// All `Add*` functions push a new component to the end of the list.
// All `Get*` functions find the first component in the list with the desired type and return 1,
// or return 0 if no such component is found.
// Therefore, when changing component values, remove the component before adding it.

// In the THPS4 binary, All add/get functions have two variants: 
//
//  * one which takes `uint32_t checksum` as the first argument
//  * one which takes `const char *name` as the first argument, computes its CRC32,
//    then calls the previous function.
//
// Here, we only point to the functions which take checksum for performance.

// We mock __thiscall calling convention by using __fastcall and a dummy second parameter.
// `this_` loaded into `ecx`, `_` loaded into `edx` and safely ignored, rest pushed onto stack.

// Any `assert` arguments should be passed 0; we don't patch the debug print functions they call.

typedef void(__fastcall* AddChecksum_t)(CStruct *this_, unused_t, uint32_t checksum, uint32_t value);
AddChecksum_t AddChecksum = (AddChecksum_t)0x00416a00; // Script::CStruct::AddChecksum

void CStruct_AddChecksum(CStruct *this_, uint32_t checksum, uint32_t value) {
    AddChecksum(this_, UNUSED, checksum, value);
}

typedef void(__fastcall* AddFloat_t)(CStruct *this_, unused_t, uint32_t checksum, float value);
AddFloat_t AddFloat = (AddFloat_t)0x00416830; // Script::CStruct::AddFloat

void CStruct_AddFloat(CStruct *this_, uint32_t checksum, float value) {
    AddFloat(this_, UNUSED, checksum, value);
}

typedef void(__fastcall* AddInteger_t)(CStruct *this_, unused_t, uint32_t checksum, int value);
AddInteger_t AddInteger = (AddInteger_t)0x00416660; // Script::CStruct::AddInteger

void CStruct_AddInteger(CStruct *this_, uint32_t checksum, int value) {
    AddInteger(this_, UNUSED, checksum, value);
}

typedef void(__fastcall* AddString_t)(CStruct *this_, unused_t, uint32_t checksum, char *value);
AddString_t AddString = (AddString_t)0x00036d14; // Script::CStruct::AddString

void CStruct_AddString(CStruct *this_, uint32_t checksum, char *value) {
    AddString(this_, UNUSED, checksum, value);
}

typedef int(__fastcall* GetChecksum_t)(CStruct *this_, unused_t, uint32_t checksum, uint32_t *ret, int assert);
GetChecksum_t GetChecksum = (GetChecksum_t)0x004184b0; // Script::CStruct::GetChecksum

int CStruct_GetChecksum(CStruct *this_, uint32_t checksum, uint32_t *ret, int assert) {
    return GetChecksum(this_, UNUSED, checksum, ret, assert);
}

typedef int(__fastcall* GetFloat_t)(CStruct *this_, unused_t, uint32_t checksum, float *ret, int assert);
GetFloat_t GetFloat = (GetFloat_t)0x00418100; // Script::CStruct::GetFloat

int CStruct_GetFloat(CStruct *this_, uint32_t checksum, float *ret, int assert) {
    return GetFloat(this_, UNUSED, checksum, ret, assert);
}

typedef int(__fastcall* GetInteger_t)(CStruct *this_, unused_t, uint32_t checksum, int *ret, int assert);
GetInteger_t GetInteger = (GetInteger_t)0x00417ea0; // Script::CStruct::GetInteger

int CStruct_GetInteger(CStruct *this_, uint32_t checksum, int *ret, int assert) {
    return GetInteger(this_, UNUSED, checksum, ret, assert);
}

typedef int(__fastcall* GetString_t)(CStruct *this_, unused_t, uint32_t checksum, const char **ret, int assert);
GetString_t GetString = (GetString_t)0x00417ff0; // Script::CStruct::GetString

int CStruct_GetString(CStruct *this_, uint32_t checksum, const char **ret, int assert) {
    return GetString(this_, UNUSED, checksum, ret, assert);
}

typedef void(__fastcall* RemoveComponent_t)(CStruct *this_, unused_t, uint32_t checksum);
RemoveComponent_t RemoveComponent = (RemoveComponent_t)0x00415b20; // Script::CStruct::RemoveComponent

void CStruct_RemoveComponent(CStruct *this_, uint32_t checksum) {
    RemoveComponent(this_, UNUSED, checksum);
}

CStruct *CScript_GetParams(CScript *this_) {
    // inlined during compilation; access member directly
    return *(CStruct **)(((char *)this_) + 0x14);
}

