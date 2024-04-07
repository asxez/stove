//
// Created by asxe on 2024/1/31.
//

#include <string.h>
#include "../lexicalParser/include/parser.h"
#include "../vm/core.h"

//执行脚本文件
static void runFile(const char *path) {
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash != NULL) {
        char *root = (char *) malloc(lastSlash - path + 2);
        memcpy(root, path, lastSlash - path + 1);
        root[lastSlash - path + 1] = EOS;
        rootDir = root;
    }

    VM *vm = newVM();
    printf("bbb");
    const char *sourceCode = readFile(path);
    executeModule(vm, OBJ_TO_VALUE(newObjString(vm, path, strlen(path))), sourceCode);
}

int main(int argc, const char **argv) {
    if (argc == 1)
        ;
    else
        runFile(argv[1]);
    return 0;
}
