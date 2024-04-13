//
// Created by asxe on 2024/1/31.
//

#include <string.h>
#include "../lexicalParser/include/parser.h"
#include "../vm/core.h"

#define MAX_LINE_LEN 1024


#ifdef _WIN32
#define OS "Windows"

#elif defined(__APPLE__) && defined(__MACH__)
#define OS "macOS"

#elif defined(__linux__)
#define OS "Linux"

#else
#define OS "Unknown"
#endif

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
    const char *sourceCode = readFile(path);
    executeModule(vm, OBJ_TO_VALUE(newObjString(vm, path, strlen(path))), sourceCode);
}

//运行命令行
static void runCli(void) {
    VM *vm = newVM();
    char sourceLine[MAX_LINE_LEN];
    printf("Stove 0.1 on %s (%s)\n", OS, vm->buildTime);
    while (true) {
        printf(">>> ");

        //若读取失败或者键入quit就退出循环
        if (!fgets(sourceLine, MAX_LINE_LEN, stdin) || memcmp(sourceLine, "quit", 4) == 0)
            break;
        executeModule(vm, OBJ_TO_VALUE(newObjString(vm, "cli", 3)), sourceLine);
    }
    freeVM(vm);
}

int main(int argc, const char **argv) {
    if (argc == 1)
        runCli();
    else
        runFile(argv[1]);
    return 0;
}
