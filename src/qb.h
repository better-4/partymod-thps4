#ifndef _QB_H_
#define _QB_H_

#include <stdint.h>

#define TYPE_STRUCTURE 0xA
#define CARRAY_SIZE 0xC
#define CSTRUCT_SIZE 0x8

typedef void CArray;
typedef void CStruct;
typedef void CScript;


// CArray::
CArray *CArray_New();
void CArray_Free(CArray *this_);

void CArray_SetStructure(CArray *this_, uint32_t index, CStruct *value);
void CArray_SetSizeAndType(CArray *this_, uint32_t size, uint32_t type);

// CStruct::
CStruct *CStruct_New();
void CStruct_Free(CStruct *this_);

void CStruct_AddArray(CStruct *this_, uint32_t checksum, CArray *value);
void CStruct_AddFloat(CStruct *this_, uint32_t checksum, float value);
void CStruct_AddInteger(CStruct *this_, uint32_t checksum, int value);
void CStruct_AddString(CStruct *this_, uint32_t checksum, char *value);
int CStruct_GetChecksum(CStruct *this_, uint32_t checksum, uint32_t *ret, int assert);
int CStruct_GetFloat(CStruct *this_, uint32_t checksum, float *ret, int assert);
int CStruct_GetInteger(CStruct *this_, uint32_t checksum, int *ret, int assert);
int CStruct_GetString(CStruct *this_, uint32_t checksum, const char **ret, int assert);
void CStruct_RemoveComponent(CStruct *this_, uint32_t checksum);

// CScript::
CStruct *CScript_GetParams(CScript *this_);

#endif
