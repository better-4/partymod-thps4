#ifndef _INPUT_H_
#define _INPUT_H_

#include <qb.h>

int processIntroEvent();
void patchInput();

int CFunc_SetSpinKeysControl(CStruct *params);
int CFunc_SetSpineTransferControl(CStruct *params);

#endif
