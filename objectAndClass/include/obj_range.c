//
// Created by asxe on 2024/2/16.
//

#include "obj_range.h"
#include "../../utils/utils.h"
#include "class.h"
#include "../../vm/vm.h"

ObjRange *newObjRange(VM *vm, int from, int to) {
    ObjRange *objRange = ALLOCATE(vm, ObjRange);
    initObjHeader(vm, &objRange->objHeader, OT_RANGE, vm->rangeClass);
    objRange->from = from;
    objRange->to = to;
    return objRange;
}
