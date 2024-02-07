//
// Created by asxe on 2024/2/1.
//

#ifndef EZLINGO_META_OBJ_H
#define EZLINGO_META_OBJ_H

#include "obj_string.h"

typedef struct {
    ObjHeader objHeader;
    SymbolTable moduleVarName; //模块中的模块变量名
    ValueBuffer moduleVarValue; //模块中的模块变量值
    ObjString *name; //模块名
} ObjModule; //模块对象

typedef struct {
    ObjHeader objHeader;
    //具体的字段
    Value fields[0];
} ObjInstance; //对象实例

ObjModule *newObjModule(VM *vm, const char *modName);
ObjInstance *newObjInstance(VM *vm, Class *class);

#endif //EZLINGO_META_OBJ_H
