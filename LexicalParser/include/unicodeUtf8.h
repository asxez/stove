//
// Created by asxe on 2024/1/27.
//

#ifndef STOVE_UNICODEUTF8_H
#define STOVE_UNICODEUTF8_H

#include <stdint.h>

uint32_t getByteNumOfEncodeUtf8(int value);
uint32_t getByteNumOfDecodeUtf8(uint8_t byte);
uint8_t encodeUtf8(uint8_t *buf, int value);
int decodeUtf8(const uint8_t *bytePtr, uint32_t length);

#endif //STOVE_UNICODEUTF8_H
