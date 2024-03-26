//
// Created by asxe on 2024/1/26.
//

#include "vm.h"
#include <stdlib.h>
#include "../utils/utils.h"
#include "core.h"
#include "../objectAndClass/include/obj_thread.h"

//初始化虚拟机
void initVM(VM *vm) {
    vm->allocatedBytes = 0;
    vm->allObjects = NULL;
    vm->curParser = NULL;
    StringBufferInit(&vm->allMethodNames);
    vm->allModules = newObjMap(vm);
}

VM *newVM(void) {
    VM *vm = (VM *) malloc(sizeof(VM));
    if (vm == NULL)
        MEM_ERROR("allocate VM memory failed.");
    initVM(vm);
    buildCore(vm);
    return vm;
}

//确保stack有效
void ensureStack(VM *vm, ObjThread *objThread, uint32_t neededSlots) {
    if (objThread->stackCapacity >= neededSlots)
        return;

    uint32_t newStackCapacity = ceilToPowerOf2(neededSlots);
    ASSERT(newStackCapacity > objThread->stackCapacity, "newStackCapacity error.");

    //记录原栈底以用于下面判断扩容后的栈是否是原地扩容
    Value *oldStackBottom = objThread->stack;

    uint32_t slotSize = sizeof(Value);
    objThread->stack = (Value *) memManager(vm, objThread->stack, objThread->stackCapacity * slotSize,
                                            newStackCapacity * slotSize);
    objThread->stackCapacity = newStackCapacity;

    //判断是否是原地扩容
    long offset = objThread->stack - oldStackBottom;

    //说明os无法在原地满足内存需求，重新分配了起始地址，下面要调整
    if (offset != 0) {
        //调整各堆栈框架的地址
        uint32_t idx = 0;
        while (idx < objThread->usedFrameNum)
            objThread->frames[idx++].stackStart += offset;

        //调整open Upvalue
        ObjUpvalue *upvalue = objThread->openUpvalues;
        while (upvalue != NULL) {
            upvalue->localVarPtr += offset;
            upvalue = upvalue->next;
        }

        //更新栈顶
        objThread->esp += offset;
    }
}

//为objClosure在objThread中创建运行时栈
static void createFrame(VM *vm, ObjThread *objThread, ObjClosure *objClosure, int argNum) {
    if (objThread->usedFrameNum + 1 > objThread->frameCapacity) {
        uint32_t newCapacity = objThread->frameCapacity * 2;
        uint32_t frameSize = sizeof(Frame);
        objThread->frames = (Frame *) memManager(vm, objThread->frames, frameSize * objThread->frameCapacity,
                                                 frameSize * newCapacity);
        objThread->frameCapacity = newCapacity;
    }

    //栈大小等于栈顶减去栈底
    uint32_t stackSlots = (uint32_t) (objThread->esp - objThread->stack);
    //总共需要的栈大小
    uint32_t neededSlots = stackSlots + objClosure->fun->maxStackSlotUsedNum;

    ensureStack(vm, objThread, neededSlots);

    //准备上CPU
    prepareFrame(objThread, objClosure, objThread->esp - argNum);
}
