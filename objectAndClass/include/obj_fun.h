//
// Created by asxe on 2024/2/1.
//

#ifndef STOVE_OBJ_FUN_H
#define STOVE_OBJ_FUN_H

#include "../../utils/utils.h"
#include "meta_obj.h"

typedef struct {
    char *funName; //函数名
    IntBuffer lineNo; //行号
} FunDebug; //函数中的调试结构

typedef struct {
    ObjHeader objHeader;
    ByteBuffer instrStream; //函数编译后的指令流
    ValueBuffer constants; //函数中的常量表

    ObjModule *module; //本函数所属的模块

    //本函数最多需要的栈空间，是栈使用空间的峰值
    uint32_t maxStackSlotUsedNum;
    uint32_t upvalueNum; //本函数所涵盖的upvalue数量
    uint8_t argNum; //函数形参个数

#if DEBUG
    FunDebug *debug;
#endif
} ObjFun; //函数对象

typedef struct upvalue {
    ObjHeader objHeader;

    //栈是个Value类型的数组，localVarPtr指向upvalue所关联的局部变量
    Value *localVarPtr;

    //已被关闭的upvalue
    Value closedUpvalue;

    struct upvalue *next; //链接openUpvalue的链表
} ObjUpvalue; //upvalue对象

typedef struct {
    ObjHeader objHeader;
    ObjFun *fun; //闭包中所要引用的函数
    ObjUpvalue *upvalues[0]; //用于存储此函数的closedUpvalue
} ObjClosure; //闭包对象

typedef struct {
    uint8_t *ip; //程序计数器，指向下一个将被执行的指令
    //在本frame中执行的闭包函数
    ObjClosure *closure;

    //frame是共享thread.stack
    //此项用于指向本frame所在thread运行时栈的起始地址
    Value *stackStart;
} Frame; //调用框架，即函数的调用帧栈

#define INITIAL_FRAME_NUM 4

ObjUpvalue *newObjUpvalue(VM *vm, Value *localVarPtr);
ObjClosure *newObjClosure(VM *vm, ObjFun *objFun);
ObjFun *newObjFun(VM *vm, ObjModule *objModule, uint32_t maxStackSlotUsedNum);

#endif //STOVE_OBJ_FUN_H
