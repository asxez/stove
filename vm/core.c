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
}                                                                                                                                       \



// 读取源代码文件
char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        IO_ERROR("There was an error while opening the file : \"%s\"", path);

    struct stat fileStat;
    stat(path, &fileStat);
    size_t fileSize = fileStat.st_size;
    char *fileContent = (char *)malloc(fileSize + 1);
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
