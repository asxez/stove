//
// Created by asxe on 2024/2/16.
//

#ifndef STOVE_OBJ_RANGE_H
#define STOVE_OBJ_RANGE_H

#include "class.h"

typedef struct {
    ObjHeader objHeader;
    int from; //起始
    int to; //终止
} ObjRange; //range对象

ObjRange *newObjRange(VM *vm, int from, int to);

#endif //STOVE_OBJ_RANGE_H
