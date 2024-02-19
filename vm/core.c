//
// Created by asxe on 2024/1/26.
//

/*
 * 实现源码的读取
 * */

#include "core.h"
#include <string.h>
#include <sys/stat.h>
#include "../utils/utils.h"
#include "vm.h"
#include "../objectAndClass/include/class.h"

char *rootDir = NULL; //根目录

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

//读取源代码文件
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

//执行模块
VMResult executeModule(VM *vm, Value moduleName, const char *moduleCode) {
    return VM_RESULT_ERROR;
}

//编译核心模块
void buildCore(VM *vm) {
    //创建核心模块，录入到vm->allModules
    ObjModule *coreModule = newObjModule(vm, NULL);
    mapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));
}
