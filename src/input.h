#ifndef _INPUT_H_
#define _INPUT_H_

#include <qb.h>

int processIntroEvent();
void patchInput();

int CFunc_SetSpinKeysControl(CStruct *params);
int CFunc_SetSpineTransferControl(CStruct *params);
int CFunc_SetPauseOnUnfocus(CStruct *params);

extern uint8_t local_observing;
void SnapObsCameraBack(void);
void ObsInputDisabled(void);

#endif
