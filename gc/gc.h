//
// Created by asxe on 2024/4/11.
//

#ifndef STOVE_GC_H
#define STOVE_GC_H
#include "../vm/vm.h"

void grayObject(VM *vm, ObjHeader *obj);
void grayValue(VM *vm, Value value);
void startGC(VM *vm);

#endif //STOVE_GC_H
