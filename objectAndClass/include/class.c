//
// Created by asxe on 2024/2/1.
//

#include "class.h"
#include <string.h>
#include "obj_range.h"
#include "../../vm/core.h"
#include "../../compiler/compiler.h"

DEFINE_BUFFER_METHOD(Method)

//判断a和b是否相等
bool valueIsEqual(Value a, Value b) {
    //类型不同则无需进行后面的比较
    if (a.type != b.type)
        return false;

    if (a.type == VT_NUM)
        return a.num == b.num;

    if (a.objHeader == b.objHeader)
        return true;

    if (a.objHeader->objType != b.objHeader->objType)
        return false;

    if (a.objHeader->objType == OT_STRING) {
        ObjString *strA = VALUE_TO_OBJSTR(a);
        ObjString *strB = VALUE_TO_OBJSTR(b);
        return (strA->value.length == strB->value.length &&
                       memcmp(strA->value.start, strB->value.start, strA->value.length) == 0);
    }

    if (a.objHeader->objType == OT_RANGE) {
        ObjRange *rgA = VALUE_TO_OBJRANGE(a);
        ObjRange *rgB = VALUE_TO_OBJRANGE(b);
        return (rgA->from == rgB->from && rgA->to == rgB->to);
    }

    return false;
}

//新建裸类
Class *newRawClass(VM *vm, const char *name, uint32_t fieldNum) {
    Class *class = ALLOCATE(vm, Class);

    //裸类无元类
    initObjHeader(vm, &class->objHeader, OT_CLASS, NULL);
    class->name = newObjString(vm, name, strlen(name));
    class->fieldNum = fieldNum;
    class->superClass = NULL; //裸类无基类
    MethodBufferInit(&class->methods);
    return class;
}

//创建一个类
Class *newClass(VM *vm, ObjString *className, uint32_t fieldNum, Class *superClass) {
    //10表示strlen(" metaClass")
#define MAX_METACLASS_LEN (MAX_ID_LEN + 10)
    char newClassName[MAX_METACLASS_LEN] = {EOS};
#undef MAX_METACLASS_LEN

    memcpy(newClassName, className->value.start, className->value.length);
    memcpy(newClassName + className->value.length, " metaClass", 10);

    //先创建子类的meta类
    Class *metaClass = newRawClass(vm, newClassName, 0);
    metaClass->objHeader.class = vm->classOfClass;

    //绑定classOfClass为meta类的基类，所有类的meta类的基类都是classOfClass
    bindSuperClass(vm, metaClass, vm->classOfClass);

    //最后创建类
    memcpy(newClassName, className->value.start, className->value.length);
    newClassName[className->value.length] = EOS;
    Class *class = newRawClass(vm, newClassName, fieldNum);

    className->objHeader.class = metaClass;
    bindSuperClass(vm, class, superClass);

    return class;
}

//数字等Value也被视为对象，因此参数为Value，获得对象obj所属的类
Class *getClassOfObj(VM *vm, Value object) {
    switch (object.type) {
        case VT_NULL:
            return vm->nullClass;
        case VT_FALSE:
        case VT_TRUE:
            return vm->boolClass;
        case VT_NUM:
            return vm->numClass;
        case VT_OBJ:
            return VALUE_TO_OBJ(object)->class;
        default:
            NOT_REACHED()
    }
    return NULL;
}
