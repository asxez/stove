//
// Created by asxe on 2024/1/26.
//

#include "vm.h"
#include <stdlib.h>
#include "../utils/utils.h"
#include "core.h"
#include "../objectAndClass/include/obj_thread.h"
#include "../compiler/compiler.h"

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

//关闭在栈中slot为lastSlot及之上的upvalue
static void closedUpvalue(ObjThread *objThread, Value *lastSlot) {
    ObjUpvalue *upvalue = objThread->openUpvalues;
    while (upvalue != NULL && upvalue->localVarPtr >= lastSlot) {
        //localVarPtr改指向本结构中的closedUpvalue
        upvalue->closedUpvalue = *(upvalue->localVarPtr);
        upvalue->localVarPtr = &(upvalue->closedUpvalue);
        upvalue = upvalue->next;
    }
    objThread->openUpvalues = upvalue;
}

//创建线程已打开的upvalue链表，并将localVarPtr所属的upvalue以降序插入到该链表
static ObjUpvalue *createOpenUpvalue(VM *vm, ObjThread *objThread, Value *localVarPtr) {
    //如果openUpvalue链表为空就创建
    if (objThread->openUpvalues == NULL) {
        objThread->openUpvalues = newObjUpvalue(vm, localVarPtr);
        return objThread->openUpvalues;
    }

    //下面以upvalue.localVarPtr降序组织openUpvalues
    ObjUpvalue *preUpvalue = NULL;
    ObjUpvalue *upvalue = objThread->openUpvalues;

    //后面的代码保证了openUpvalues按照降序组织，下面向堆栈的底部遍历，直到找到合适的插入位置
    while (upvalue != NULL && upvalue->localVarPtr > localVarPtr) {
        preUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    //如果之前已经插入了该upvalue则返回
    if (upvalue != NULL && upvalue->localVarPtr == localVarPtr)
        return upvalue;

    //openUpvalues中未找到该upvalue，现在就创建新upvalue，按照降序插入到链表
    ObjUpvalue *newUpvalue = newObjUpvalue(vm, localVarPtr);

    //保证了openUpvalues首节点upvalue->localVarPtr的值是最高的
    if (preUpvalue == NULL)
        //说明上面的while循环未执行，新节点 （形参localVarPtr）的值大于等于链表首节点，因此使链表节点指向它所在的新upvalue节点
        objThread->openUpvalues = newUpvalue;
    else
        //preUpvalue已处于正确位置
        preUpvalue->next = newUpvalue;

    newUpvalue->next = upvalue;
    return newUpvalue; //返回该节点
}

//校验基类合法性
static void validateSuperClass(VM *vm, Value classNameValue, uint32_t fieldNum, Value superClassValue) {
    //首先确保superClass的类型是class
    if (!VALUE_IS_CLASS(superClassValue)) {
        ObjString *classNameString = VALUE_TO_OBJSTR(classNameValue);
        RUN_ERROR("class \"%s\" 's superClass is not a valid class.", classNameString->value.start);
    }

    Class *superClass = VALUE_TO_CLASS(superClassValue);
    //基类不允许为内建类
    if (
            superClass == vm->stringClass ||
            superClass == vm->mapClass ||
            superClass == vm->rangeClass ||
            superClass == vm->listClass ||
            superClass == vm->nullClass ||
            superClass == vm->boolClass ||
            superClass == vm->numClass ||
            superClass == vm->funClass ||
            superClass == vm->threadClass
            )
        RUN_ERROR("superClass must not be a builtin class.");

    //子类也要继承基类的域，故子类自己的域+基类域的数量不可超过MAX_FIELD_NUM
    if (superClass->fieldNum + fieldNum > MAX_FIELD_NUM)
        RUN_ERROR("number of field including super exceed %d.", MAX_FIELD_NUM);
}

//修正部分指令操作数
static void patchOperand(Class *class, ObjFun *fun) {
    int ip = 0;
    OpCode opCode;
    while (true) {
        opCode = (OpCode) fun->instrStream.datas[ip++];
        switch (opCode) {
            case OPCODE_LOAD_FIELD:
            case OPCODE_STORE_FIELD:
            case OPCODE_LOAD_SELF_FIELD:
            case OPCODE_STORE_SELF_FIELD:
                //修正子类的field数量，参数是1字节
                fun->instrStream.datas[ip++] += class->superClass->fieldNum;
                break;
            case OPCODE_SUPER0:
            case OPCODE_SUPER1:
            case OPCODE_SUPER2:
            case OPCODE_SUPER3:
            case OPCODE_SUPER4:
            case OPCODE_SUPER5:
            case OPCODE_SUPER6:
            case OPCODE_SUPER7:
            case OPCODE_SUPER8:
            case OPCODE_SUPER9:
            case OPCODE_SUPER10:
            case OPCODE_SUPER11:
            case OPCODE_SUPER12:
            case OPCODE_SUPER13:
            case OPCODE_SUPER14:
            case OPCODE_SUPER15:
            case OPCODE_SUPER16: {
                //指令流1：2字节的method索引
                //指令流2：2字节的基类常量索引
                ip += 2; //跳过2字节的method索引
                uint32_t superClassIdx = (fun->instrStream.datas[ip] << 8) | fun->instrStream.datas[ip + 1];

                //回填在函数emitCallBySignature中的占位VT_TO_VALUE(VT_NULL)
                fun->constants.datas[superClassIdx] = OBJ_TO_VALUE(class->superClass);
                ip += 2; //跳过2字节的基类索引
                break;
            }
            case OPCODE_CREATE_CLOSURE: {
                //指令流：2字节待创建闭包的函数在常量表中的索引+函数所用的upvalue数 * 2
                //函数是存储到常量表中，获取待创建闭包的函数常量表中的索引
                uint32_t funIdx = (fun->instrStream.datas[ip] << 8) | fun->instrStream.datas[ip + 1];

                //递归进入该函数的指令流，继续为其中的super和field修正操作数
                patchOperand(class, VALUE_TO_OBJFUN(fun->constants.datas[funIdx]));

                //ip - 1是操作码OPCODE_CREATE_CLOSURE
                //闭包中的参数涉及upvalue，调用getBytesOfOperands获得参数字节数
                ip += getBytesOfOperands(fun->instrStream.datas, fun->constants.datas, ip - 1);
                break;
            }
            case OPCODE_END:
                //用于从当前及递归嵌套闭包时返回
                return;
            default:
                //其他指令不需要回填因此就跳过
                ip += getBytesOfOperands(fun->instrStream.datas, fun->constants.datas, ip - 1);
                break;
        }
    }
}

//绑定方法和修正操作数
static void bindMethodAndPatch(VM *vm, OpCode opCode, uint32_t methodIndex, Class *class, Value methodValue) {
    //如果是静态方法，就将类指向meta类（使接收者为meta类）
    if (opCode == OPCODE_STATIC_METHOD)
        class = class->objHeader.class;

    Method method;
    method.methodType = MT_SCRIPT;
    method.obj = VALUE_TO_OBJCLOSURE(methodValue);

    //修正操作数
    patchOperand(class, method.obj->fun);

    //修正过后，绑定method到class
    bindMethod(vm, class, methodIndex, method);
}
