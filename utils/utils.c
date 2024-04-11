#include "common.h"
#include "utils.h"
#include "../vm/vm.h"
#include "../lexicalParser/include/parser.h"
#include <stdlib.h>
#include <stdarg.h>

void *memManager(VM *vm, void *ptr, uint32_t oldSize, uint32_t newSize) {
    //累计系统分配的总内存
    vm->allocatedBytes += newSize - oldSize;

    //避免realloc(NULL, 0)定义的新地址，此地址不能被释放
    if (newSize == 0) {
        free(ptr);
        return NULL;
    }

    //在分配内存时若达到了GC触发的阈值则启动垃圾回收
    if (newSize > 0 && vm->allocatedBytes > vm->config.nextGC)
        startGC(vm);

    return realloc(ptr, newSize);
}

uint32_t ceilToPowerOf2(uint32_t v) {
    v = (v == 0) ? 1 : v; //修复当v等于0时结果为0的边界情况
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

DEFINE_BUFFER_METHOD(String)

DEFINE_BUFFER_METHOD(Int)

DEFINE_BUFFER_METHOD(Char)

DEFINE_BUFFER_METHOD(Byte)

void symbolTableClear(VM *vm, SymbolTable *buffer) {
    uint32_t idx = 0;
    while (idx < buffer->count)
        memManager(vm, buffer->datas[idx++].str, 0, 0);
    StringBufferClear(vm, buffer);
}

//通用报错函数
void errorReport(void *parser, ErrorType errorType, const char *fmt, ...) {
    char buffer[DEFAULT_BUFFER_SIZE] = {EOS};
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, DEFAULT_BUFFER_SIZE, fmt, ap);
    va_end(ap);

    switch (errorType) {
        case ERROR_IO:
        case ERROR_MEM:
            fprintf(stderr, "%s:%d in function %s():%s\n", __FILE__, __LINE__, __func__, buffer);
            break;
        case ERROR_LEX:
        case ERROR_COMPILE:
            ASSERT(parser != NULL, "parser is null");
            fprintf(stderr, "%s:%dd \"%s\"\n", ((Parser *) parser)->file, ((Parser *) parser)->preToken.lineNo, buffer);
            break;
        case ERROR_RUNTIME:
            fprintf(stderr, "%s\n", buffer);
            break;
        default:
            NOT_REACHED()
    }
    exit(EXIT_FAILURE);
}
