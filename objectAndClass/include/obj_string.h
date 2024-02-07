//
// Created by asxe on 2024/2/1.
//

#ifndef STOVE_OBJ_STRING_H
#define STOVE_OBJ_STRING_H

#include "header_obj.h"

typedef struct {
    ObjHeader objHeader;
    uint32_t hashCode; //字符串的哈希值
    CharValue value;
} ObjString;

#define ROTL32(x, r) ((x << r) | (x >> (32 - r)))

uint32_t fnvLaHashString(char *str, uint32_t length);
uint32_t mm3HashString(const char *str, uint32_t length, uint32_t seed);
void hashObjString(ObjString *objString);
ObjString *newObjString(VM *vm, const char *str, uint32_t length);

#endif //STOVE_OBJ_STRING_H
