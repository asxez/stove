//
// Created by asxe on 2024/1/31.
//

#include <stdio.h>
#include <string.h>
#include "../lexicalParser/include/parser.h"
#include "../vm/vm.h"
#include "../vm/core.h"
#include "../lexicalParser/include/token.list"
#include "../objectAndClass/include/class.h"

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

    struct parser parser;
    initParser(vm, &parser, path, sourceCode, NULL);

    while (parser.curToken.tokenType != TOKEN_EOF) {
        getNextToken(&parser);
        printf("%dL : %s [", parser.curToken.lineNo, tokenArray[parser.curToken.tokenType]);
        uint32_t idx = 0;
        while (idx < parser.curToken.length)
            printf("%c", *(parser.curToken.start + idx++));
        printf("]\n");
    }
}

int main(int argc, const char **argv) {
    if (argc == 1)
        ;
    else
        runFile(argv[1]);
    return 0;
}
