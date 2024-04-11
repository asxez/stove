//
// Created by asxe on 2024/4/11.
//

#include "gc.h"
#include "../compiler/compiler.h"
#include "../objectAndClass/include/obj_list.h"
#include "../objectAndClass/include/obj_range.h"

#if DEBUG
#include "debug.h"
#include <time.h>
#endif

//标记obj为灰色：即把obj收集到数组vm->grays.grayObjects
void grayObject(VM *vm, ObjHeader *obj) {
    //如果isDark为true则为黑色，说明已经可达，直接返回
    if (obj == NULL || obj->isDark)
        return;

    //标记为可达
    obj->isDark = true;

    //若超过容量则扩容
    if (vm->grays.count >= vm->grays.capacity) {
        vm->grays.capacity = vm->grays.count * 2;
        vm->grays.grayObjects = (ObjHeader **) realloc(vm->grays.grayObjects, vm->grays.capacity * sizeof(ObjHeader *));
    }

    //把obj添加到数组grayObjects
    vm->grays.grayObjects[vm->grays.count++] = obj;
}

//标灰value
void grayValue(VM *vm, Value value) {
    //只有对象才需要标记
    if (!VALUE_IS_OBJ(value))
        return;
    grayObject(vm, VALUE_TO_OBJ(value));
}

//标灰buffer->datas中的value
static void grayBuffer(VM *vm, ValueBuffer *buffer) {
    uint32_t idx = 0;
    while (idx < buffer->count) {
        grayValue(vm, buffer->datas[idx]);
        idx++;
    }
}

//标黑class
static void blackClass(VM *vm, Class *class) {
    //标灰meta类
    grayObject(vm, (ObjHeader *) class->objHeader.class);

    //标灰父类
    grayObject(vm, (ObjHeader *) class->superClass);

    //标灰方法
    uint32_t idx = 0;
    while (idx < class->methods.count) {
        if (class->methods.datas[idx].methodType == MT_SCRIPT)
            grayObject(vm, (ObjHeader *) class->methods.datas[idx].obj);
        idx++;
    }

    //标灰类名
    grayObject(vm, (ObjHeader *) class->name);

    //累计类大小
    vm->allocatedBytes += sizeof(Class);
    vm->allocatedBytes += sizeof(Method) * class->methods.capacity;
}

//标灰闭包
static void blackClosure(VM *vm, ObjClosure *objClosure) {
    //标灰闭包中的函数
    grayObject(vm, (ObjHeader *) objClosure->fun);

    //标灰闭包中的upvalue
    uint32_t idx = 0;
    while (idx < objClosure->fun->upvalueNum) {
        grayObject(vm, (ObjHeader *) objClosure->upvalues[idx]);
        idx++;
    }

    //累计闭包大小
    vm->allocatedBytes += sizeof(ObjClosure);
    vm->allocatedBytes += sizeof(ObjUpvalue *) * objClosure->fun->upvalueNum;
}

//标黑objThread
static void blackThread(VM *vm, ObjThread *objThread) {
    //标灰frame
    uint32_t idx = 0;
    while (idx < objThread->usedFrameNum) {
        grayObject(vm, (ObjHeader *) objThread->frames[idx].closure);
        idx++;
    }

    //标灰运行时栈中每个slot
    Value *slot = objThread->stack;
    while (slot < objThread->esp) {
        grayValue(vm, *slot);
        slot++;
    }

    //标灰本线程中所有的upvalue
    ObjUpvalue *upvalue = objThread->openUpvalues;
    while (upvalue != NULL) {
        grayObject(vm, (ObjHeader *) upvalue);
        upvalue = upvalue->next;
    }

    //标灰caller
    grayObject(vm, (ObjHeader *) objThread->caller);
    grayValue(vm, objThread->errorObj);

    //累计线程大小
    vm->allocatedBytes += sizeof(ObjThread);
    vm->allocatedBytes += objThread->frameCapacity * sizeof(Frame);
    vm->allocatedBytes += objThread->stackCapacity * sizeof(Value);
}

//标黑Fun
static void blackFun(VM *vm, ObjFun *fun) {
    //标灰常量
    grayBuffer(vm, &fun->constants);

    //累计ObjFun空间
    vm->allocatedBytes += sizeof(ObjFun);
    vm->allocatedBytes += sizeof(uint8_t *) * fun->instrStream.capacity;
    vm->allocatedBytes += sizeof(Value) * fun->constants.capacity;

#if DEBUG
    //再加上debug信息占用的内存
    vm.allocatedBytes += sizeof(Int) * fun.instrStream.capacity;
#endif
}

//标黑objInstance
static void blackInstance(VM *vm, ObjInstance *objInstance) {
    //标灰元类
    grayObject(vm, (ObjHeader *) objInstance->objHeader.class);

    //标灰实例中所有域，域的个数在class->fieldNum
    uint32_t idx = 0;
    while (idx < objInstance->objHeader.class->fieldNum) {
        grayValue(vm, objInstance->fields[idx]);
        idx++;
    }

    //累计objInstance空间
    vm->allocatedBytes += sizeof(ObjInstance);
    vm->allocatedBytes += sizeof(Value) * objInstance->objHeader.class->fieldNum;
}

//标黑objList
static void blackList(VM *vm, ObjList *objList) {
    //标灰列表的elements
    grayBuffer(vm, &objList->elements);

    //累计objList大小
    vm->allocatedBytes += sizeof(ObjList);
    vm->allocatedBytes += sizeof(Value) * objList->elements.capacity;
}

//标黑objMap
static void blackMap(VM *vm, ObjMap *objMap) {
    //标灰所有entry
    uint32_t idx = 0;
    while (idx < objMap->capacity) {
        Entry *entry = &objMap->entries[idx];
        //跳过无效的entry
        if (!VALUE_IS_UNDEFINED(entry->key)) {
            grayValue(vm, entry->key);
            grayValue(vm, entry->value);
        }
        idx++;
    }

    //累计objMap大小
    vm->allocatedBytes += sizeof(ObjMap);
    vm->allocatedBytes += sizeof(Entry *) * objMap->capacity;
}

//标黑objModule
static void blackModule(VM *vm, ObjModule *objModule) {
    //标灰模块中所有模块变量
    uint32_t idx = 0;
    while (idx < objModule->moduleVarValue.count) {
        grayValue(vm, objModule->moduleVarValue.datas[idx]);
        idx++;
    }

    //标灰模块名
    grayObject(vm, (ObjHeader *) objModule->name);

    //累计objModule大小
    vm->allocatedBytes += sizeof(ObjModule);
    vm->allocatedBytes += sizeof(String) * objModule->moduleVarName.capacity;
    vm->allocatedBytes += sizeof(Value) * objModule->moduleVarValue.capacity;
}

//标黑range
static void blackRange(VM *vm) {
    //ObjRange中没有大数据，只有from和to，其空间属于sizeof(ObjRange)，因此不用额外标记
    vm->allocatedBytes += sizeof(ObjRange);
}

//标黑objString
static void blackString(VM *vm, ObjString *objString) {
    //累计ObjString空间+1是结尾\0
    vm->allocatedBytes += sizeof(ObjString) + objString->value.length + 1;
}

//标黑objUpvalue
static void blackUpvalue(VM *vm, ObjUpvalue *objUpvalue) {
    //标灰objUpvalue的closedUpvalue
    grayValue(vm, objUpvalue->closedUpvalue);

    //累计objUpvalue大小
    vm->allocatedBytes += sizeof(ObjUpvalue);
}

//标黑对象
static void blackObject(VM *vm, ObjHeader *obj) {
#if DEBUG
    printf("mark ");
    dumpValue(OBJ_TO_VALUE(obj));
    printf(" @ %p\n", obj);
#endif

    //根据对象类型标黑
    switch (obj->objType) {
        case OT_CLASS:
            blackClass(vm, (Class *) obj);
            break;
        case OT_CLOSURE:
            blackClosure(vm, (ObjClosure *) obj);
            break;
        case OT_THREAD:
            blackThread(vm, (ObjThread *) obj);
            break;
        case OT_FUNCTION:
            blackFun(vm, (ObjFun *) obj);
            break;
        case OT_INSTANCE:
            blackInstance(vm, (ObjInstance *) obj);
            break;
        case OT_LIST:
            blackList(vm, (ObjList *) obj);
            break;
        case OT_MAP:
            blackMap(vm, (ObjMap *) obj);
            break;
        case OT_MODULE:
            blackModule(vm, (ObjModule *) obj);
            break;
        case OT_RANGE:
            blackRange(vm);
            break;
        case OT_STRING:
            blackString(vm, (ObjString *) obj);
            break;
        case OT_UPVALUE:
            blackUpvalue(vm, (ObjUpvalue *) obj);
            break;
    }
}



