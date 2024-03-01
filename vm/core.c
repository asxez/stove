//
// Created by asxe on 2024/1/26.
//

#include "core.h"
#include <string.h>
#include <sys/stat.h>
#include "../utils/utils.h"
#include "vm.h"
#include "../objectAndClass/include/class.h"

char *rootDir = NULL; // 根目录

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

// 返回值类型是Value类型，且是放在args[0]，args是Value类型数组
// RET_VALUE的参数是Value类型，无须转换直接赋值，它是“RET_”的基础
#define RET_VALUE(value) \
    do {                 \
        args[0] = value; \
        return true;     \
    } while (0);

// 将obj转换成Value后作为返回值
#define RET_OBJ(objPtr) RET_VALUE(OBJ_TO_VALUE(objPtr))

// 将布尔值转为Value后作为返回值
#define RET_BOOL(boolean) RET_VALUE(BOOL_TO_VALUE(boolean))
#define RET_NUM(num) RET_VALUE(NUM_TO_VALUE(num))
#define RET_NULL RET_VALUE(VT_TO_VALUE(VT_NULL))
#define RET_TRUE RET_VALUE(VT_TO_VALUE(VT_TRUE))
#define RET_FALSE RET_VALUE(VT_TO_VALUE(VT_FALSE))

// 设置线程报错
#define SET_ERROR_FALSE(vmPtr, errMsg)                                                          \
    do {                                                                                        \
        vmPtr->curThread->errorObj = OBJ_TO_VALUE(newObjString(vmPtr, errMsg, strlen(errMsg))); \
        return false;                                                                           \
    } while (0);

// 绑定方法func到classPtr指向的类
#define PRIM_METHOD_BIND(classPtr, methodName, func) {                                    \
        uint32_t length = strlen(methodName);                                             \
        int globalIdx = getIndexFromSymbolTable(&vm->allMethodNames, methodName, length); \
        if (globalIdx == -1)                                                              \
            globalIdx = addSymbol(vm, &vm->allMethodNames, methodName, length);           \
        Method method;                                                                    \
        method.type = MT_PRIMITIVE;                                                       \
        method.primFun = func;                                                            \
        bindMethod(vm, classPtr, (uint32_t) globalIdx, method);                           \
}

// 读取源代码文件
char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        IO_ERROR("There was an error while opening the file : \"%s\"", path);

    struct stat fileStat;
    stat(path, &fileStat);
    size_t fileSize = fileStat.st_size;
    char *fileContent = (char *) malloc(fileSize + 1);
    if (fileContent == NULL)
        MEM_ERROR("Couldn't allocate memory for reading file : \"%s\"", path);

    size_t numRead = fread(fileContent, sizeof(char), fileSize, file);
    if (numRead < fileSize) {
        fclose(file);
        free(fileContent);
        IO_ERROR("Couldn't read file : \"%s\"", path);
    }
    fileContent[fileSize] = EOS;
    fclose(file);
    return fileContent;
}

// 执行模块
VMResult executeModule(VM *vm, Value moduleName, const char *moduleCode) {
    return VM_RESULT_ERROR;
}

// 编译核心模块
void buildCore(VM *vm) {
    // 创建核心模块，录入到vm->allModules
    ObjModule *coreModule = newObjModule(vm, NULL);
    mapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));
}

// !object: object取反，结果为false
static bool primObjectNot(VM *vm UNUSED, Value *args) {
    RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

//args[0] == args[1]: 返回object是否相等
static bool primObjectEqual(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue);
}

//args[0] != args[1]: 返回object是否不等
static bool primObjectNotEqual(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(!valueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue);
}

// args[0] is args[1]:类args[0]是否为类args[1]的子类
static bool primObjectIsSonClass(VM *vm, Value *args) {
    //args[1]必须是class
    if (!VALUE_IS_CLASS(args[1]))
        RUN_ERROR("argument must be class.");

    Class *thisClass = getClassOfObj(vm, args[0]);
    Class *baseClass = (Class *) (args[1].objHeader);

    //有可能是多级继承，因此自下而上遍历基类链
    while (baseClass != NULL) {
        //在某一级基类找到匹配，就设置返回值为VT_TRUE并返回
        if (thisClass == baseClass)
            RET_VALUE(VT_TO_VALUE(VT_TRUE));
        baseClass = baseClass->superClass;
    }

    //若未找到基类，说明不具备is_a关系
    RET_VALUE(VT_TO_VALUE(VT_FALSE));
}

//args[0].toString:返回args[0]所属class的名字
static bool primObjectToString(VM *vm UNUSED, Value *args) {
    Class *class = args[0].objHeader->class;
    Value nameValue = OBJ_TO_VALUE(class->name);
    RET_VALUE(nameValue);
}

//args[0].type:返回对象args[0]的类
static bool primObjectType(VM *vm, Value *args) {
    Class *class = getClassOfObj(vm, args[0]);
    RET_OBJ(class);
}

//args[0].name：返回类名
static bool primClassName(VM *vm UNUSED, Value *args) {
    RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

//args[0].superType:返回args[0]的基类
static bool primClassSuperType(VM *vm UNUSED, Value *args) {
    Class *class = VALUE_TO_CLASS(args[0]);
    if (class->superClass != NULL)
        RET_OBJ(class->superClass);
    RET_VALUE(VT_TO_VALUE(VT_NULL));
}

//args[0].toString:返回类名
static bool primClassToString(VM *vm UNUSED, Value *args) {
    RET_OBJ(VALUE_TO_CLASS(args[0])->name);
}

//args[0].same(args[1],args[2]):返回args[1]和args[2]是否相等
static bool primObjectMetaSame(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[1], args[2]));
    RET_VALUE(boolValue);
}

//table中查找符号symbol，找到后返回索引，否则返回-1
int getIndexFromSymbolTable(SymbolTable *table, const char *symbol, uint32_t length) {
    ASSERT(length != 0, "length of symbol is 0.");
    uint32_t index = 0;
    while (index < table->count) {
        if (length == table->datas[index].length && memcmp(table->datas[index].str, symbol, length) == 0)
            return (int) index;
        index++;
    }
    return -1;
}

//往table中添加符号symbol，返回其索引
int addSymbol(VM *vm, SymbolTable *table, const char *symbol, uint32_t length) {
    ASSERT(length != 0, "length of symbol is 0.");
    String string;
    string.str = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(string.str, symbol, length);
    string.str[length] = EOS;
    string.length = length;
    StringBufferAdd(vm, table, string);
    return (int) (table->count - 1);
}







