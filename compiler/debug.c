//
// Created by asxe on 2024/4/13.
//

#ifdef DEBUG
#include "debug.h"
#include <stdio.h>
#include "../vm/vm.h"
#include <string.h>
#include "../objectAndClass/include/class.h"

//在funDebug中绑定函数名
void bindDebugFunName(VM *vm, FunDebug *funDebug, const char *name, uint32_t length) {
    ASSERT(funDebug->funName == NULL, "debug.name has bound.");
    funDebug->funName = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(funDebug->funName, name, length);
    funDebug->funName[length] = EOS;
}

//打印栈
void dumpStack(ObjThread *thread) {
    printf("(thread %p) stack:%p, esp:%p, slots:%ld", thread, thread->stack, thread->esp, thread->esp - thread->stack);
    Value *slot = thread->stack;
    while (slot < thread->esp) {
        dumpValue(*slot);
        printf(" | ");
        slot++;
    }
    printf("\n");
}

//打印对象
static void dumpObject(ObjHeader *obj) {
    switch (obj->objType) {
        case OT_CLASS:
            printf("[class %s %p]", ((Class *) obj)->name->value.start, obj);
            break;
        case OT_CLOSURE:
            printf("[closure %p]", obj);
            break;
        case OT_THREAD:
            printf("[thread %p]", obj);
            break;
        case OT_FUNCTION:
            printf("[fun %p]", obj);
            break;
        case OT_INSTANCE:
            printf("[instance %p]", obj);
            break;
        case OT_LIST:
            printf("[list %p]", obj);
            break;
        case OT_MAP:
            printf("[map %p]", obj);
            break;
        case OT_MODULE:
            printf("[module %p]", obj);
            break;
        case OT_RANGE:
            printf("[range %p]", obj);
            break;
        case OT_STRING:
            printf("%s", ((ObjString *) obj)->value.start);
            break;
        case OT_UPVALUE:
            printf("[upvalue %p]", obj);
            break;
        default:
            printf("[unknown object %d]", obj->objType);
            break;
    }
}

//打印value
void dumpValue(Value value) {
    switch (value.type) {
        case VT_FALSE:
            printf("false");
            break;
        case VT_TRUE:
            printf("true");
            break;
        case VT_NULL:
            printf("null");
            break;
        case VT_NUM:
            printf("%.14g", VALUE_TO_NUM(value));
            break;
        case VT_OBJ:
            dumpObject(VALUE_TO_OBJ(value));
            break;
        case VT_UNDEFINED:
            NOT_REACHED()
    }
}

//打印一条指令
static int dumpOneInstruction(VM *vm, ObjFun *fun, int i, int *lastLine) {
    int start = i;
    uint8_t *bytecode = fun->instrStream.datas;
    OpCode opCode = (OpCode) bytecode[i];

    int lineNo = fun->debug->lineNo.datas[i];

    if (lastLine == NULL || *lastLine != lineNo) {
        printf("%4d:", lineNo); //输出源码行号
        if (lastLine != NULL)
            *lastLine = lineNo;
    } else
        printf("    ");

    printf(" %04d  ", i++); //输出指令流中的位置

#define READ_BYTE() (bytecode[i++])
#define READ_SHORT() (i += 2, (bytecode[i - 2] << 8) | bytecode[i - 1])

#define BYTE_INSTRUCTION(name) \
    printf("%-16s %5d\n", name, READ_BYTE()); \
    break; \

    switch (opCode) {
        case OPCODE_LOAD_CONSTANT: {
            int constant = READ_SHORT();
            printf("%-16s %5d '", "LOAD_CONSTANT", constant);
            dumpValue(fun->constants.datas[constant]);
            printf("'\n");
            break;
        }
        case OPCODE_PUSH_NULL:
            printf("PUSH_NULL\n");
            break;
        case OPCODE_PUSH_FALSE:
            printf("PUSH_FALSE\n");
            break;
        case OPCODE_PUSH_TRUE:
            printf("PUSH_TRUE\n");
            break;

        case OPCODE_LOAD_LOCAL_VAR:
        BYTE_INSTRUCTION("LOAD_LOCAL_VAR")
        case OPCODE_STORE_LOCAL_VAR:
        BYTE_INSTRUCTION("STORE_LOCAL_VAR")
        case OPCODE_LOAD_UPVALUE:
        BYTE_INSTRUCTION("LOAD_UPVALUE")
        case OPCODE_STORE_UPVALUE:
        BYTE_INSTRUCTION("STORE_UPVALUE")

        case OPCODE_LOAD_MODULE_VAR: {
            int slot = READ_SHORT();
            printf("-16%s %5d '%s'\n", "STORE_MODULE_VAR", slot, fun->module->moduleVarName.datas[slot].str);
            break;
        }
        case OPCODE_STORE_MODULE_VAR: {
            int slot = READ_SHORT();
            printf("-16%s %5d '%s'\n", "STORE_MODULE_VAR", slot, fun->module->moduleVarName.datas[slot].str);
            break;
        }

        case OPCODE_LOAD_SELF_FIELD:
        BYTE_INSTRUCTION("LOAD_SELF_FIELD")
        case OPCODE_STORE_SELF_FIELD:
        BYTE_INSTRUCTION("STORE_SELF_FIELD")
        case OPCODE_LOAD_FIELD:
        BYTE_INSTRUCTION("LOAD_FIELD")
        case OPCODE_STORE_FIELD:
        BYTE_INSTRUCTION("STORE_FIELD")

        case OPCODE_POP:
            printf("POP\n");
            break;

        case OPCODE_CALL0:
        case OPCODE_CALL1:
        case OPCODE_CALL2:
        case OPCODE_CALL3:
        case OPCODE_CALL4:
        case OPCODE_CALL5:
        case OPCODE_CALL6:
        case OPCODE_CALL7:
        case OPCODE_CALL8:
        case OPCODE_CALL9:
        case OPCODE_CALL10:
        case OPCODE_CALL11:
        case OPCODE_CALL12:
        case OPCODE_CALL13:
        case OPCODE_CALL14:
        case OPCODE_CALL15:
        case OPCODE_CALL16: {
            int numArgs = bytecode[i - 1] - OPCODE_CALL0;
            int symbol = READ_SHORT();
            printf("CALL%-11d %5d '%s'\n", numArgs, symbol, vm->allMethodNames.datas[symbol].str);
            break;
        }

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
            int numArgs = bytecode[i - 1] - OPCODE_SUPER0;
            int symbol = READ_SHORT();
            int superclass = READ_SHORT();
            printf("SUPER%-10d %5d '%s' %5d\n", numArgs, symbol, vm->allMethodNames.datas[symbol].str, superclass);
            break;
        }

        case OPCODE_JUMP: {
            int offset = READ_SHORT();
            printf("%-16s offset:%-5d abs:%d\n", "JUMP", offset, i + offset);
            break;
        }

        case OPCODE_LOOP: {
            int offset = READ_SHORT();
            printf("%-16s offset:%-5d abs:%d\n", "LOOP", offset, i - offset);
            break;
        }

        case OPCODE_JUMP_IF_FALSE: {
            int offset = READ_SHORT();
            printf("%-16s offset:%-5d abs:%d\n", "JUMP_IF_FALSE", offset, i + offset);
            break;
        }

        case OPCODE_AND: {
            int offset = READ_SHORT();
            printf("%-16s offset:%-5d abs:%d\n", "AND", offset, i + offset);
            break;
        }

        case OPCODE_OR: {
            int offset = READ_SHORT();
            printf("%-16s offset:%-5d abs:%d\n", "OR", offset, i + offset);
            break;
        }

        case OPCODE_CLOSE_UPVALUE:
            printf("CLOSE_UPVALUE\n");
            break;

        case OPCODE_RETURN:
            printf("RETURN\n");
            break;

        case OPCODE_CREATE_CLOSURE: {
            int constant = READ_SHORT();
            printf("%-16s %5d ", "CREATE_CLOSURE", constant);
            dumpValue(fun->constants.datas[constant]);
            printf(" ");
            ObjFun *loadeFun = VALUE_TO_OBJFUN(fun->constants.datas[constant]);
            uint32_t j;
            for (j = 0; j < loadeFun->upvalueNum; j++) {
                int isLocal = READ_BYTE();
                int index = READ_BYTE();
                if (j > 0)
                    printf(", ");
                printf("%s %d", isLocal ? "local" : "upvalue", index);
            }
            printf("\n");
            break;
        }

        case OPCODE_CONSTRUCT:
            printf("CONSTRUCT\n");
            break;

        case OPCODE_CREATE_CLASS: {
            int numFields = READ_BYTE();
            printf("%-16s %5d fields\n", "CREATE_CLASS", numFields);
            break;
        }

        case OPCODE_INSTANCE_METHOD: {
            int symbol = READ_SHORT();
            printf("%-16s %5d '%s'\n", "INSTANCE_METHOD", symbol, vm->allMethodNames.datas[symbol].str);
            break;
        }

        case OPCODE_STATIC_METHOD: {
            int symbol = READ_SHORT();
            printf("%-16s %5d '%s'\n", "STATIC_METHOD", symbol, vm->allMethodNames.datas[symbol].str);
            break;
        }

        case OPCODE_END:
            printf("END\n");
            break;

        default:
            printf("UNKNOWN! [%d]\n", bytecode[i - 1]);
            break;
    }

    //返回指令占用的字节数
    if (opCode == OPCODE_END)
        return -1;
    return i - start;

#undef READ_BYTE
#undef READ_SHORT
}

//打印指令
void dumpInstructions(VM *vm, ObjFun *fun) {
    printf("module:[%s]\tfunction:[%s]\n", fun->module->name == NULL ? "<core>" : fun->module->name->value.start,
           fun->debug->funName);
    int i = 0;
    int lastLine = -1;
    while (true) {
        int offset = dumpOneInstruction(vm, fun, i, &lastLine);
        if (offset == -1)
            break;
        i += offset;
    }
    printf("\n");
}
#endif
