//
// Created by asxe on 2024/2/1.
//

#include "obj_fun.h"
#include "class.h"

//创建一个空函数
ObjFun *newObjFun(VM *vm, ObjModule *objModule, uint32_t slotNum) {
    ObjFun *objFun = ALLOCATE(vm, ObjFun);
    if (objFun == NULL)
        MEM_ERROR("allocate ObjFun failed.");
    initObjHeader(vm, &objFun->objHeader, OT_FUNCTION, vm->funClass);
    ByteBufferInit(&objFun->instrStream);
    ValueBufferInit(&objFun->constants);
    objFun->module = objModule;
    objFun->maxStackSlotUsedNum = slotNum;
    objFun->upvalueNum = objFun->argNum = 0;
#ifdef DEBUG
    objFun->debug = ALLOCATE(vm, FunDebug);
    objFun->debug->funName = NULL;
    IntBufferInit(&objFun->debug->lineNo);
#endif
    return objFun;
}

//以函数fun创建一个闭包
ObjClosure *newObjClosure(VM *vm, ObjFun *objFun) {
    ObjClosure *objClosure = ALLOCATE_EXTRA(vm, ObjClosure, sizeof(ObjUpvalue *) * objFun->upvalueNum);
    initObjHeader(vm, &objClosure->objHeader, OT_CLOSURE, vm->funClass);
    objClosure->fun = objFun;

    //清除upvalue数组，以避免在填充upvalue数组之前触发GC
    uint32_t idx = 0;
    while (idx < objFun->upvalueNum) {
        objClosure->upvalues[idx] = NULL;
        idx++;
    }

    return objClosure;
}

//创建upvalue对象
ObjUpvalue *newObjUpvalue(VM *vm, Value *localVarPtr) {
    ObjUpvalue *objUpvalue = ALLOCATE(vm, ObjUpvalue);
    initObjHeader(vm, &objUpvalue->objHeader, OT_UPVALUE, NULL);
    objUpvalue->localVarPtr = localVarPtr;
    objUpvalue->closedUpvalue = VT_TO_VALUE(VT_NULL);
    objUpvalue->next = NULL;
    return objUpvalue;
}
