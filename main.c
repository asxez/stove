//typedef struct {
//    uint32_t length;
//    char start[0];
//} CharValue; // 字符串缓冲区
//
//typedef struct {
//    uint32_t hashCode; //字符串的哈希值
//    CharValue value;
//} ObjString;
//
//static int findString(ObjString *haystack, ObjString *needle);
#include "stdio.h"
#include "stdint.h"
#include "time.h"
int main() {
    printf("%f", (double)time(NULL));
    return 0;
}
