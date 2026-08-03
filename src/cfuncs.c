#include "cfuncs.h"

#include <log.h>
#include <patch.h>
#include <string.h>

#define THPS4_CFUNC_LUT_START 0x005aba40
// #define THPS4_CFUNC_LUT_STOP 0x005ad670
#define THPS4_CFUNC_LUT_SIZE 0x22f8
#define THPS4_NUM_CFUNCS 0x386

#define BETTER4_NUM_CFUNCS 2
#define NUM_CFUNCS (THPS4_NUM_CFUNCS + BETTER4_NUM_CFUNCS)


CFunc cfuncs[NUM_CFUNCS];

void initCFuncs() {
    // memcpy(&cfuncs, (void *)THPS4_CFUNC_LUT_START, sizeof (CFunc) * THPS4_NUM_CFUNCS);
    memcpy(&cfuncs, THPS4_CFUNC_LUT_START, sizeof(CFunc) * THPS4_NUM_CFUNCS);
}

int cfunc_index = THPS4_NUM_CFUNCS;
void addCFunc(const char *name, void *func) {
    CFunc *cfunc = &cfuncs[cfunc_index++];
    cfunc->name = name;
    cfunc->func = func;
}

void printCFuncs() {
    // sanity check: verify we registered the # of cfuncs we reserved for better4
    if (cfunc_index != NUM_CFUNCS) {
        printLog("WARNING: registered %d cfuncs, expected %d\n", cfunc_index, NUM_CFUNCS);
    }

    printLog("printing cfuncs we own\n");
    for (int i = 0; i < NUM_CFUNCS; i++) {
        CFunc cfunc = cfuncs[i];
        printLog("%s: 0x%p\n", cfunc.name, cfunc.func);
    }
}

void patchCFuncs() {
    patchWord(0x00511f40 + 1, NUM_CFUNCS); // Script::GetCFunctionLookupTableSize
    patchDWord(0x00512180 + 1, &cfuncs);  // Script::CFunctionLookupTable
}
