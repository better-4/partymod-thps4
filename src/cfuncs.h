#ifndef _CFUNCS_H_
#define _CFUNCS_H_

typedef struct {
    char* name;
    void* func;
} CFunc;

void initCFuncs();
void addCFunc(const char *name, void *func);
void printCFuncs();
void patchCFuncs();

#endif
