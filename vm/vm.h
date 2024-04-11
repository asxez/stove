//
// Created by asxe on 2024/1/26.
//

#ifndef STOVE_VM_H
#define STOVE_VM_H

#include "../utils/common.h"
#include "../objectAndClass/include/header_obj.h"
#include "../objectAndClass/include/obj_map.h"
#include "../objectAndClass/include/obj_thread.h"

#define MAX_TEMP_ROOTS_NUM 8

#define OPCODE_SLOTS(opcode, effect) OPCODE_##opcode,
typedef enum {
    #include "opcode.inc"
} OpCode;
#undef OPCODE_SLOTS

typedef enum vmResult {
    VM_RESULT_SUCCESS,
    VM_RESULT_ERROR,
} VMResult; //虚拟机执行的结果，若执行无误，可以将字符码输出到文件缓存，避免下次重新编译

//灰色对象信息结构
typedef struct {
    //gc中的灰色对象（也是保留对象）指针数组
    ObjHeader **grayObjects;
    uint32_t capacity;
    uint32_t count;
} Gray;

typedef struct {
    int heapGrowthFactor; //堆生长因子
    uint32_t initialHeapSize; //初始堆大小，默认10MB
    uint32_t minHeapSize; //最小堆大小，默认1MB
    uint32_t nextGC; //第一次触发gc的堆大小，默认为initialHeapSize
} Configuration;

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
    ObjThread *curThread; //当前正在执行的线程
    Parser *curParser; //当前词法分析器

    //临时的跟对象集合（数组），存储临时需要被GC保留的对象，避免回收
    ObjHeader *tmpRoots[MAX_TEMP_ROOTS_NUM];
    uint32_t tmpRootNum;

    //用于存储保留的对象
    Gray grays;
    Configuration config;
};

void initVM(VM *vm);
VM *newVM(void);
void pushTmpRoot(VM *vm, ObjHeader *obj);
void popTmpRoot(VM *vm);
void ensureStack(VM *vm, ObjThread *objThread, uint32_t neededSlots);
VMResult executeInstruction(VM *vm, register ObjThread *curThread);

#endif //STOVE_VM_H
