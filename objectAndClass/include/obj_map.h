//
// Created by asxe on 2024/2/16.
//

#ifndef STOVE_OBJ_MAP_H
#define STOVE_OBJ_MAP_H

#include "header_obj.h"

#define MAP_LOAD_PERCENT 0.8

typedef struct {
    Value key;
    Value value;
} Entry; //键值对

typedef struct {
    ObjHeader objHeader;
    uint32_t capacity; //Entry容量
    uint32_t count; //使用的Entry数量
    Entry *entries;
} ObjMap;

ObjMap *newObjMap(VM *vm);

void mapSet(VM *vm, ObjMap *objMap, Value key, Value value);
void mapGet(ObjMap *objMap, Value key);
void clearMap(VM *vm, ObjMap *objMap);
Value removeKey(VM *vm, ObjMap *objMap, Value key);

#endif //STOVE_OBJ_MAP_H
