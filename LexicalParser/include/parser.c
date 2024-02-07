//
// Created by asxe on 2024/1/28.
//

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "utils.h"
#include "unicodeUtf8.h"
#include <string.h>
#include <ctype.h>
#include "../../objectAndClass/include/obj_string.h"
#include "../../objectAndClass/include/class.h"

struct keywordToken {
    char *keyword;
    uint8_t length;
    TokenType token;
}; //关键字结构体

//关键字查找表
struct keywordToken keywordsToken[] = {
        {"var",      3, TOKEN_VAR},
        {"define",   6, TOKEN_DEFINE},
        {"if",       2, TOKEN_IF},
        {"else",     4, TOKEN_ELSE},
        {"true",     4, TOKEN_TRUE},
        {"false",    5, TOKEN_FALSE},
        {"while",    5, TOKEN_WHILE},
        {"for",      3, TOKEN_FOR},
        {"break",    5, TOKEN_BREAK},
        {"continue", 8, TOKEN_CONTINUE},
        {"return",   6, TOKEN_RETURN},
        {"null",     4, TOKEN_NULL},
        {"class",    5, TOKEN_CLASS},
        {"is",       2, TOKEN_IS},
        {"static",   6, TOKEN_STATIC},
        {"self",     4, TOKEN_SELF},
        {"super",    5, TOKEN_SUPER},
        {"import",   6, TOKEN_IMPORT},
        {NULL,       0, TOKEN_UNKNOWN}
};

//判断start是否为关键字并返回响应的token
static TokenType idOrKeyword(const char *start, uint32_t length) {
    uint32_t idx = 0;
    while (keywordsToken[idx].keyword != NULL) {
        if (keywordsToken[idx].length == length && memcmp(keywordsToken[idx].keyword, start, length) == 0)
            return keywordsToken[idx].token;
        idx++;
    }
    return TOKEN_ID;
}

//向前看一个字符
char peekAheadChar(Parser *parser) {
    return *parser->nextCharPtr;
}

//获取下一个字符
static void getNextChar(Parser *parser) {
    parser->curChar = *parser->nextCharPtr++;
}

//查看下一个字符是否为期望的，如果是则读并返回true，否则返回false
static bool matchNextChar(Parser *parser, char expectedChar) {
    if (peekAheadChar(parser) == expectedChar) {
        getNextChar(parser);
        return true;
    }
    return false;
}

//跳过连续的空白字符
static void skipBlanks(Parser *parser) {
    while (isspace(parser->curChar)) {
        if (parser->curChar == '\n')
            parser->curToken.lineNo++;
        getNextChar(parser);
    }
}

//解析标识符
static void parseId(Parser *parser, TokenType type) {
    while (isalnum(parser->curChar) || parser->curChar == '_')  //int isalnum(int c)  检查c是否是字母和数字，是则返回非0值，反之返回0
        getNextChar(parser);

    //nextCharPtr会指向1个不合法字符的下一个字符，因此要-1
    uint32_t length = (uint32_t) (parser->nextCharPtr - parser->curToken.start - 1);
    if (type != TOKEN_UNKNOWN)
        parser->curToken.tokenType = type;
    else
        parser->curToken.tokenType = idOrKeyword(parser->curToken.start, length);
    parser->curToken.length = length;
}

//解析十六进制数字
static void parseHexNum(Parser *parser) {
    while (isxdigit(parser->curChar))
        getNextChar(parser);
}

//解析十进制数字
static void parseDecNum(Parser *parser) {
    while (isdigit(parser->curChar))
        getNextChar(parser);

    //若有小数点
    if (parser->curChar == '.' && isdigit(peekAheadChar(parser))) {
        getNextChar(parser);
        while (isdigit(parser->curChar))
            getNextChar(parser);
    }
}

//解析八进制数字
static void parseOctNum(Parser *parser) {
    while (parser->curChar >= '0' && parser->curChar <= '8')
        getNextChar(parser);
}

//解析八进制、十进制、十六进制，仅支持前缀形式
static void parseNum(Parser *parser) {
    //十六进制的0x前缀
    if (parser->curChar == '0' && matchNextChar(parser, 'x')) {
        getNextChar(parser);
        parseHexNum(parser);
        parser->curToken.value = NUM_TO_VALUE(strtol(parser->curToken.start, NULL, 16));
    } else if (parser->curChar == '0' && isdigit(peekAheadChar(parser))) {
        parseOctNum(parser);
        parser->curToken.value = NUM_TO_VALUE(strtol(parser->curToken.start, NULL, 8));
    } else {
        parseDecNum(parser);
        parser->curToken.value = NUM_TO_VALUE(strtod(parser->curToken.start, NULL));
    }
    //nextCharPtr会指向第一个不合法字符的下一个字符，因此-1
    parser->curToken.length = (uint32_t) (parser->nextCharPtr - parser->curToken.start - 1);
    parser->curToken.tokenType = TOKEN_NUM;
}

//解析unicode码点
static void parseUnicodePoint(Parser *parser, ByteBuffer *buf) {
    uint32_t idx = 0;
    int value = 0;
    uint8_t digit = 0;

    //获取数值，u后面跟着4位十六进制的数字
    while (idx++ < 4) {
        getNextChar(parser);
        if (parser->curChar == EOS)
            LEX_ERROR(parser, "unterminated unicode.");
        if (parser->curChar >= '0' && parser->curChar <= '9')
            digit = parser->curChar - '0';
        else if (parser->curChar >= 'a' && parser->curChar <= 'f')
            digit = parser->curChar - 'a' + 10;
        else if (parser->curChar >= 'A' && parser->curChar <= 'F')
            digit = parser->curChar - 'A' + 10;
        else
            LEX_ERROR(parser, "invalid unicode");
        value = value * 16 | digit;
    }

    uint32_t byteNum = getByteNumOfEncodeUtf8(value);
    ASSERT(byteNum != 0, "utf8 encode bytes should be between 1 and 4.");

    //为代码通用，直接写buf->datas，在此先写个byteNum个0，以保证事先有byteNum个空间
    ByteBufferFillWrite(parser->vm, buf, 0, byteNum);

    //把value编码为utf8后写入缓冲区
    encodeUtf8(buf->datas + buf->count - byteNum, value);
}

//解析字符串
static void parseString(Parser *parser) {
    ByteBuffer str;
    ByteBufferInit(&str);

    while (true) {
        getNextChar(parser);

        if (parser->curChar == EOS)
            LEX_ERROR(parser, "unterminated string.");

        if (parser->curChar == '"') {
            parser->curToken.tokenType = TOKEN_STRING;
            break;
        }

        if (parser->curChar == '%') {
            if (!matchNextChar(parser, '('))
                LEX_ERROR(parser, "'%' should followed by '('.");
            if (parser->interpolationExpectRightParenNum > 0) //若此时此值大于0，说明前面已经有一个%号了，并期望一个)，而此时又来一个%，则说明此时出现了双重嵌套，故不支持
                COMPILE_ERROR(parser, "not support nest interpolate expression.");
            parser->interpolationExpectRightParenNum = 1;
            parser->curToken.tokenType = TOKEN_INTERPOLATION;
            break;
        }

        if (parser->curChar == '\\') {
            getNextChar(parser);
            switch (parser->curChar) {
                case '0':
                    ByteBufferAdd(parser->vm, &str, '\0');
                    break;
                case 'a':
                    ByteBufferAdd(parser->vm, &str, '\a');
                    break;
                case 'b':
                    ByteBufferAdd(parser->vm, &str, '\b');
                    break;
                case 'f':
                    ByteBufferAdd(parser->vm, &str, '\f');
                    break;
                case 'n':
                    ByteBufferAdd(parser->vm, &str, '\n');
                    break;
                case 'r':
                    ByteBufferAdd(parser->vm, &str, '\r');
                    break;
                case 't':
                    ByteBufferAdd(parser->vm, &str, '\t');
                    break;
                case 'u':
                    parseUnicodePoint(parser, &str);
                    break;
                case '"':
                    ByteBufferAdd(parser->vm, &str, '"');
                    break;
                case '\\':
                    ByteBufferAdd(parser->vm, &str, '\\');
                    break;
                default:
                    LEX_ERROR(parser, "not support escape \\%c", parser->curChar);
                    break;
            }
        } else  //普通字符
            ByteBufferAdd(parser->vm, &str, parser->curChar);
    }
    //用识别到的字符串新建字符串对象存储到curToken的value中
    ObjString *objString = newObjString(parser->vm, (const char *) str.datas, str.count);
    parser->curToken.value = OBJ_TO_VALUE(objString);
    ByteBufferClear(parser->vm, &str);
}

//跳过一行
static void skipAline(Parser *parser) {
    getNextChar(parser);
    while (parser->curChar != EOS) {
        if (parser->curChar == '\n') {
            parser->curToken.lineNo++;
            getNextChar(parser);
            break;
        }
        getNextChar(parser);
    }
}

//跳过行注释和块注释
static void skipComment(Parser *parser) {
    char nextChar = peekAheadChar(parser);
    if (parser->curChar == '/')
        skipAline(parser);
    else {
        while (nextChar != '*' && nextChar != EOS) {
            getNextChar(parser);
            if (parser->curChar == '\n')
                parser->curToken.lineNo++;
            nextChar = peekAheadChar(parser);
        }
        if (matchNextChar(parser, '*')) {
            if (!matchNextChar(parser, '/'))
                LEX_ERROR(parser, "expect '/' after '*'.");
            getNextChar(parser);
        } else
            LEX_ERROR(parser, "expect '*/' before file end.");
    }
    skipBlanks(parser);
}

//获得下一个token
void getNextToken(Parser *parser) {
    parser->preToken = parser->curToken;
    skipBlanks(parser); //逃过待识别单词前的空格
    parser->curToken.tokenType = TOKEN_EOF;
    parser->curToken.length = 0;
    parser->curToken.start = parser->nextCharPtr - 1;
    while (parser->curChar != EOS) {
        switch (parser->curChar) {
            case ',':
                parser->curToken.tokenType = TOKEN_COMMA;
                break;
            case ':':
                parser->curToken.tokenType = TOKEN_COLON;
                break;
            case '(':
                if (parser->interpolationExpectRightParenNum > 0)
                    parser->interpolationExpectRightParenNum++;
                parser->curToken.tokenType = TOKEN_LEFT_PAREN;
                break;
            case ')':
                if (parser->interpolationExpectRightParenNum > 0) {
                    parser->interpolationExpectRightParenNum--;
                    if (parser->interpolationExpectRightParenNum == 0) {
                        parseString(parser);
                        break;
                    }
                }
                parser->curToken.tokenType = TOKEN_RIGHT_PAREN;
                break;
            case '[':
                parser->curToken.tokenType = TOKEN_LEFT_BRACKET;
                break;
            case ']':
                parser->curToken.tokenType = TOKEN_RIGHT_BRACKET;
                break;
            case '{':
                parser->curToken.tokenType = TOKEN_LEFT_BRACE;
                break;
            case '}':
                parser->curToken.tokenType = TOKEN_RIGHT_BRACE;
                break;
            case '.':
                if (matchNextChar(parser, '.'))
                    parser->curToken.tokenType = TOKEN_DOT_DOT;
                else
                    parser->curToken.tokenType = TOKEN_DOT;
                break;
            case '=':
                if (matchNextChar(parser, '='))
                    parser->curToken.tokenType = TOKEN_EQUAL;
                else
                    parser->curToken.tokenType = TOKEN_ASSIGN;
                break;
            case '+':
                parser->curToken.tokenType = TOKEN_ADD;
                break;
            case '-':
                parser->curToken.tokenType = TOKEN_SUB;
                break;
            case '*':
                parser->curToken.tokenType = TOKEN_MUL;
                break;
            case '/':
                if (matchNextChar(parser, '/') || matchNextChar(parser, '*')) {
                    skipComment(parser);

                    //重置下一个token起始地址
                    parser->curToken.start = parser->nextCharPtr - 1;
                    continue;
                } else
                    parser->curToken.tokenType = TOKEN_DIV;
                break;
            case '%':
                parser->curToken.tokenType = TOKEN_MOD;
                break;
            case '&':
                if (matchNextChar(parser, '&'))
                    parser->curToken.tokenType = TOKEN_LOGIC_AND;
                else
                    parser->curToken.tokenType = TOKEN_BIT_AND;
                break;
            case '|':
                if (matchNextChar(parser, '|'))
                    parser->curToken.tokenType = TOKEN_LOGIC_OR;
                else
                    parser->curToken.tokenType = TOKEN_BIT_OR;
                break;
            case '~':
                parser->curToken.tokenType = TOKEN_BIT_NOT;
                break;
            case '?':
                parser->curToken.tokenType = TOKEN_QUESTION;
                break;
            case '>':
                if (matchNextChar(parser, '='))
                    parser->curToken.tokenType = TOKEN_MORE_EQUAL;
                else if (matchNextChar(parser, '>'))
                    parser->curToken.tokenType = TOKEN_BIT_SHIFT_RIGHT;
                else
                    parser->curToken.tokenType = TOKEN_MORE;
                break;
            case '<':
                if (matchNextChar(parser, '='))
                    parser->curToken.tokenType = TOKEN_LESS_EQUAL;
                else if (matchNextChar(parser, '<'))
                    parser->curToken.tokenType = TOKEN_BIT_SHIFT_LEFT;
                else
                    parser->curToken.tokenType = TOKEN_LESS;
                break;
            case '!':
                if (matchNextChar(parser, '='))
                    parser->curToken.tokenType = TOKEN_NOT_EQUAL;
                else
                    parser->curToken.tokenType = TOKEN_LOGIC_NOT;
                break;
            case '"':
                parseString(parser);
                break;
            default:
                //处理变量名和数字
                //进入此分支的字符肯定是数字或变量名的首字符

                if (isalpha(parser->curChar) || parser->curChar == '_')
                    parseId(parser, TOKEN_UNKNOWN); //解析变量名其余的部分
                else if (isdigit(parser->curChar))
                    parseNum(parser);
                else {
                    if (parser->curChar == '#' && matchNextChar(parser, '!')) {
                        skipAline(parser);
                        parser->curToken.start = parser->nextCharPtr - 1;
                        continue;
                    }
                    LEX_ERROR(parser, "not support char : \'%c\'.", parser->curChar);
                }
                return;
        }
        parser->curToken.length = (uint32_t) (parser->nextCharPtr - parser->curToken.start);
        getNextChar(parser);
        return;
    }
}

//若当前token为expected则读入下一个token并返回true，否则不读入token且返回false
bool matchToken(Parser *parser, TokenType expected) {
    if (parser->curToken.tokenType == expected) {
        getNextToken(parser);
        return true;
    }
    return false;
}

//断言当前token为expected并读入下一token，否则报错
void consumeCurToken(Parser *parser, TokenType expected, const char *errMsg) {
    if (parser->curToken.tokenType != expected)
        COMPILE_ERROR(parser, errMsg);
    getNextToken(parser);
}

//断言下一个token为expected，否则报错
void consumeNextToken(Parser *parser, TokenType expected, const char *errMsg) {
    getNextToken(parser);
    if (parser->curToken.tokenType != expected)
        COMPILE_ERROR(parser, errMsg);
}

//由于sourceCode未必来自于文件file，有可能只是个字符串
//file仅用作跟踪待编译的代码的标识，便于报错
void initParser(VM *vm, Parser *parser, const char *file, const char *sourceCode, ObjModule *objModule) {
    parser->file = file;
    parser->sourceCode = sourceCode;
    parser->curChar = *parser->sourceCode;
    parser->nextCharPtr = parser->sourceCode + 1;
    parser->curToken.lineNo = 1;
    parser->curToken.tokenType = TOKEN_UNKNOWN;
    parser->curToken.start = NULL;
    parser->curToken.length = 0;
    parser->preToken = parser->curToken;
    parser->interpolationExpectRightParenNum = 0;
    parser->vm = vm;
    parser->curModule = objModule;
}
