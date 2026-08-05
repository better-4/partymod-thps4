#ifndef _QB_H_
#define _QB_H_

#include <stdint.h>

typedef void CStruct;
typedef void CScript;

void CStruct_AddInteger(CStruct *this_, uint32_t checksum, int value);
void CStruct_AddString(CStruct *this_, uint32_t checksum, char *value);
int CStruct_GetChecksum(CStruct *this_, uint32_t checksum, uint32_t *ret, int assert);
int CStruct_GetFloat(CStruct *this_, uint32_t checksum, float *ret, int assert);
int CStruct_GetInteger(CStruct *this_, uint32_t checksum, int *ret, int assert);
int CStruct_GetString(CStruct *this_, uint32_t checksum, const char **ret, int assert);
void CStruct_RemoveComponent(CStruct *this_, uint32_t checksum);

CStruct *CScript_GetParams(CScript *this_);

#endif
