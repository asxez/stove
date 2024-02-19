//
// Created by asxe on 2024/2/1.
//

#include "obj_string.h"
#include <string.h>
#include "../../vm/vm.h"
#include "../../utils/utils.h"
#include "../../utils/common.h"
#include <stdlib.h>

//fnv-la算法
uint32_t fnvLaHashString(char *str, uint32_t length) {
    uint32_t hashCode = 2166136261, idx = 0;
    while (idx < length) {
        hashCode ^= str[idx];
        hashCode *= 16777619;
        idx++;
    }
    return hashCode;
}

//murmur3算法
uint32_t mm3HashString(const char *str, uint32_t length, uint32_t seed) {
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;
    const uint32_t r1 = 15;
    const uint32_t r2 = 13;
    const uint32_t m = 5;
    const uint32_t n = 0xe6546b64;

    uint32_t hash = seed;

    const int nblocks = length / 4;
    const uint32_t *blocks = (const uint32_t *) str;
    int i;
    uint32_t k;

    for (i = 0; i < nblocks; i++) {
        k = blocks[i];
        k *= c1;
        k = ROTL32(k, r1);
        k *= c2;

        hash ^= k;
        hash = ROTL32(hash, r2) * m + n;
    }

    const uint8_t *tail = (const uint8_t *) (str + nblocks * 4);
    uint32_t k1 = 0;

    switch (length & 3) {
        case 3:
            k1 ^= tail[2] << 16;
            // Intentional fallthrough
        case 2:
            k1 ^= tail[1] << 8;
            // Intentional fallthrough
        case 1:
            k1 ^= tail[0];
            k1 *= c1;
            k1 = ROTL32(k1, r1);
            k1 *= c2;
            hash ^= k1;
    }

    hash ^= length;
    hash ^= (hash >> 16);
    hash *= 0x85ebca6b;
    hash ^= (hash >> 13);
    hash *= 0xc2b2ae35;
    hash ^= (hash >> 16);

    return hash;
}

//为string计算哈希码并将值存储到string-hash
void hashObjString(ObjString *objString) {
    objString->hashCode = mm3HashString(objString->value.start, objString->value.length, 0);
}

ObjString *newObjString(VM *vm, const char *str, uint32_t length) {
    //length为0时str必为null，length不为0时str不为null
    ASSERT(length == 0 || str != NULL, "str length don't match str.");

    //结尾\0
    ObjString *objString = ALLOCATE_EXTRA(vm, ObjString, length + 1);

    if (objString != NULL) {
        initObjHeader(vm, &objString->objHeader, OT_STRING, vm->stringClass);
        objString->value.length = length;

        //支持空字符串：str为null，length为0，非空则复制其内容
        if (length > 0)
            memcpy(objString->value.start, str, length);
        objString->value.start[length] = EOS;
        hashObjString(objString);
    } else
        MEM_ERROR("allocating objString failed.");
    return objString;
}
