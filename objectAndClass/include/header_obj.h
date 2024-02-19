//
// Created by asxe on 2024/1/31.
//

#ifndef STOVE_HEADER_OBJ_H
#define STOVE_HEADER_OBJ_H

#include "../../utils/utils.h"

typedef enum {
    OT_CLASS, //此项是class类型，以下都是object类型
    OT_LIST,
    OT_MAP,
    OT_MODULE,
    OT_RANGE,
    OT_STRING,
    OT_UPVALUE,
    OT_FUNCTION,
    OT_CLOSURE,
    OT_INSTANCE,
    OT_THREAD
} ObjType; //对象类型

typedef struct objHeader {
    ObjType objType;
    bool isDark; //对象是否可达
    Class *class; //对象所属类
    struct objHeader *next; //链接所有分配的对象，链表
} ObjHeader; //对象头，用于记录元信息和GC

typedef enum {
    VT_UNDEFINED,
    VT_NULL,
    VT_FALSE,
    VT_TRUE,
    VT_NUM,
    VT_OBJ //值为对象，指向对象头
} ValueType; //value类型

typedef struct {
    ValueType type;
    union {
        double num;
        ObjHeader *objHeader;
    };
} Value; //通用的值结构

DECLARE_BUFFER_TYPE(Value)

void initObjHeader(VM *vm, ObjHeader *objHeader, ObjType objType, Class *class);

#endif //STOVE_HEADER_OBJ_H
