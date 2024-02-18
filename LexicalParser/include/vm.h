//
// Created by asxe on 2024/1/26.
//

#ifndef STOVE_VM_H
#define STOVE_VM_H

#include "common.h"
#include "../../objectAndClass/include/header_obj.h"

struct vm {
    Class *stringClass;
    Class *funClass;
    Class *listClass;
    Class *rangeClass;
    Class *mapClass;
    uint32_t allocatedBytes; //累计已分配的内存量
    ObjHeader *allObjects; //所有已分配对象链表
    Parser *curParser; //当前词法分析器
};

void initVM(VM *vm);
VM *newVM(void);

#endif //STOVE_VM_H
