//
// Created by asxe on 2024/2/8.
//

#ifndef STOVE_OBJ_LIST_H
#define STOVE_OBJ_LIST_H

#include "class.h"
#include "../../vm/vm.h"

typedef struct {
    ObjHeader objHeader;
    ValueBuffer elements; //元素
} ObjList; //列表对象

ObjList *newObjList(VM *vm, uint32_t elementNum);
Value removeElement(VM *vm, ObjList *objList, uint32_t index);
void insertElement(VM *vm, ObjList *objList, uint32_t index, Value value);

#endif //STOVE_OBJ_LIST_H
