//
// Created by asxe on 2024/2/8.
//

#include "obj_list.h"

//新建列表对象，元素个数为elementNum
ObjList *newObjList(VM *vm, uint32_t elementNum) {
    //存储列表元素的缓冲区
    Value *elementArray = NULL;

    //分配内存，后调用initObjHeader，避免gc无所谓的遍历
    if (elementNum > 0)
        elementArray = ALLOCATE_ARRAY(vm, Value, elementNum);
    ObjList *objList = ALLOCATE(vm, ObjList);

    objList->elements.datas = elementArray;
    objList->elements.capacity = objList->elements.count = elementNum;
    initObjHeader(vm, &objList->objHeader, OT_LIST, vm->listClass);
    return objList;
}

//在objList中索引index处插入value，类似于list[index] = value
void insertElement(VM *vm, ObjList *objList, uint32_t index, Value value) {
    if (index > objList->elements.count - 1)
        RUN_ERROR("index out bounded.");

    if (VALUE_IS_OBJ(value)) {
        pushTmpRoot(vm, VALUE_TO_OBJ(value));
    }
    //准备一个Value的空间以容纳新元素产生的空间波动，即最后一个元素要后移1个空间
    ValueBufferAdd(vm, &objList->elements, VT_TO_VALUE(VT_NULL));
    if (VALUE_IS_OBJ(value)) {
        popTmpRoot(vm);
    }

    //使index后面的元素后移
    uint32_t idx = objList->elements.count - 1;
    while (idx > index) {
        objList->elements.datas[idx] = objList->elements.datas[idx - 1];
        idx--;
    }

    objList->elements.datas[index] = value;
}

//调整列表容量
static void shrinkList(VM *vm, ObjList *objList, uint32_t newCapacity) {
    uint32_t oldSize = objList->elements.capacity * sizeof(Value);
    uint32_t newSize = newCapacity * sizeof(Value);
    memManager(vm, objList->elements.datas, oldSize, newSize);
    objList->elements.capacity = newCapacity;
}

//删除列表中索引为index处的元素，即删除list[index]
Value removeElement(VM *vm, ObjList *objList, uint32_t index) {
    Value valueRemoved = objList->elements.datas[index];

    if (VALUE_IS_OBJ(valueRemoved)) {
        pushTmpRoot(vm, VALUE_TO_OBJ(valueRemoved));
    }
    //使index后面的元素整体向前移一位
    uint32_t idx = index;
    while (idx < objList->elements.count) {
        objList->elements.datas[idx] = objList->elements.datas[idx + 1];
        idx++;
    }

    //若容量利用率过低则减小容量
    uint32_t _capacity = objList->elements.capacity / CAPACITY_GROW_FACTOR;
    if (_capacity > objList->elements.count)
        shrinkList(vm, objList, _capacity);
    if (VALUE_IS_OBJ(valueRemoved)) {
        popTmpRoot(vm);
    }

    objList->elements.count--;
    return valueRemoved;
}
