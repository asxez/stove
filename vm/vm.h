//
// Created by asxe on 2024/1/26.
//

#ifndef STOVE_VM_H
#define STOVE_VM_H

#include "../utils/common.h"
#include "../objectAndClass/include/header_obj.h"
#include "../objectAndClass/include/obj_map.h"
#include "../objectAndClass/include/obj_thread.h"

#define OPCODE_SLOTS(opcode, effect) OPCODE_##opcode,
typedef enum {
    #include "opcode.inc"
} OpCode;
#undef OPCODE_SLOTS

typedef enum vmResult {
    VM_RESULT_SUCCESS,
    VM_RESULT_ERROR,
} VMResult; //虚拟机执行的结果，若执行无误，可以将字符码输出到文件缓存，避免下次重新编译

struct vm {
    Class *stringClass;
    Class *funClass;
    Class *listClass;
    Class *rangeClass;
    Class *mapClass;
    Class *nullClass;
    Class *boolClass;
    Class *numClass;
    Class *threadClass;
    Class *objectClass;
    Class *classOfClass;
    uint32_t allocatedBytes; //累计已分配的内存量
    ObjHeader *allObjects; //所有已分配对象链表
    SymbolTable allMethodNames; //所有类的方法名
    ObjMap *allModules;
    ObjThread *curThread;
    Parser *curParser; //当前词法分析器
};

void initVM(VM *vm);
VM *newVM(void);
VMResult executeInstruction(VM *vm, register ObjThread *curThread);

#endif //STOVE_VM_H
