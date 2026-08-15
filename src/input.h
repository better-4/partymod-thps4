#ifndef _INPUT_H_
#define _INPUT_H_

// typedef struct CStruct;

int processIntroEvent();
void patchInput();

int CFunc_SetSpinKeysControl(void *params);
int CFunc_SetSpineTransferControl(void *params);
int CFunc_SetPauseOnUnfocus(void *params);

extern uint8_t local_observing;
void SnapObsCameraBack(void);
void ObsInputDisabled(void);

#endif
