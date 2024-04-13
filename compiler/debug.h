//
// Created by asxe on 2024/4/13.
//

#ifdef DEBUG
#ifndef STOVE_DEBUG_H
#define STOVE_DEBUG_H

#include "../utils/utils.h"
#include "../objectAndClass/include/obj_fun.h"
#include "../objectAndClass/include/obj_thread.h"

void bindDebugFunName(VM *vm, FunDebug *funDebug, const char *name, uint32_t length);
void dumpValue(Value value);
void dumpInstructions(VM *vm, ObjFun *fun);
void dumpStack(ObjThread *thread);

#endif //STOVE_DEBUG_H
#endif
