//
// Created by asxe on 2024/1/26.
//

#ifndef STOVE_PARSER_H
#define STOVE_PARSER_H

#include "../../utils/common.h"
#include "../../vm/vm.h"
#include "../../objectAndClass/include/meta_obj.h"
#include "../../compiler/compiler.h"

typedef enum {
    TOKEN_UNKNOWN,
    // 数据类型
    TOKEN_NUM,           // 数字类型
    TOKEN_STRING,        // 字符串类型
    TOKEN_ID,            // 变量名
    TOKEN_INTERPOLATION, // 内嵌表达式  format

    // 关键字
    TOKEN_VAR,      // var
    TOKEN_DEFINE,   // define
    TOKEN_IF,       // if
    TOKEN_ELSE,     // else
    TOKEN_TRUE,     // true
    TOKEN_FALSE,    // false
    TOKEN_WHILE,    // while
    TOKEN_FOR,      // for
    TOKEN_BREAK,    // break
    TOKEN_CONTINUE, // continue
    TOKEN_RETURN,   // return
    TOKEN_NULL,     // null

    // 类和模块导入
    TOKEN_CLASS,  // class
    TOKEN_SELF,   // self
    TOKEN_STATIC, // static
    TOKEN_IS,     // is
    TOKEN_SUPER,  // super
    TOKEN_IMPORT, // import

    // 分隔符
    TOKEN_COMMA,         // ,
    TOKEN_COLON,         // :
    TOKEN_LEFT_PAREN,    // (
    TOKEN_RIGHT_PAREN,   // )
    TOKEN_LEFT_BRACKET,  // [
    TOKEN_RIGHT_BRACKET, // ]
    TOKEN_LEFT_BRACE,    // {
    TOKEN_RIGHT_BRACE,   // }
    TOKEN_DOT,           // .
    TOKEN_DOT_DOT,       // ..

    // 双目运算符
    TOKEN_ADD, // +
    TOKEN_SUB, // -
    TOKEN_MUL, // *
    TOKEN_DIV, // /
    TOKEN_MOD, // %

    // 赋值运算符
    TOKEN_ASSIGN, // =

    // 位运算符
    TOKEN_BIT_AND,         // &
    TOKEN_BIT_OR,          // |
    TOKEN_BIT_NOT,         // ~
    TOKEN_BIT_SHIFT_RIGHT, // >>
    TOKEN_BIT_SHIFT_LEFT,  // <<

    // 逻辑运算符
    TOKEN_LOGIC_AND, // &&
    TOKEN_LOGIC_OR,  // ||
    TOKEN_LOGIC_NOT, // !

    // 关系操作符
    TOKEN_EQUAL,      // ==
    TOKEN_NOT_EQUAL,  // !=
    TOKEN_MORE,       // >
    TOKEN_MORE_EQUAL, // >=
    TOKEN_LESS,       // <
    TOKEN_LESS_EQUAL, // <=

    TOKEN_QUESTION, // ?

    TOKEN_EOF // end of file
} TokenType;

typedef struct {
    TokenType tokenType;
    const char *start; //源码串单词起始位置
    uint32_t length;   //此单词长度
    uint32_t lineNo;   //单词所在源码的行号
    Value value;
} Token;

struct parser {
    const char *file; //指向源码文件名
    const char *sourceCode; //源码串
    const char *nextCharPtr; //sourceCode中下一个字符
    char curChar; //识别到的当前字符
    Token curToken; //当前token
    Token preToken; //前一个token
    ObjModule *curModule; //当前正在编译的模块
    CompileUnit *curCompileUnit; //当前编译单元
    //词法分析器在任何时刻都会处于一个编译单元中，模块、方法、函数都有自己的编译单元。
    //类没有编译单元，因为编译单元是独立的指令流单位，类是由方法组成的，方法才是指令流的单位，类中不允许定义变量，方法中才可以有变量定义，因从类只是方法的编译单元的集合。

    //处于内嵌表达式中时，期望的右括号数量
    //用于跟踪小括号对的嵌套
    int interpolationExpectRightParenNum;
    struct parser *parent; //父parser
    VM *vm;
};

#define PEEK_TOKEN(parserPtr) parserPtr->curToken.tokenType

char peekAheadChar(Parser *parser);
void getNextToken(Parser *parser);
bool matchToken(Parser *parser, TokenType expectedTokenType);
void consumeCurToken(Parser *parser, TokenType expectedTokenType, const char *errMsg);
void consumeNextToken(Parser *parser, TokenType expectedTokenType, const char *errMsg);
uint32_t getByteNumOfEncodeUtf8(int value);
uint8_t encodeUtf8(uint8_t *buf, int value);
void initParser(VM *vm, Parser *parser, const char *file, const char *sourceCode, ObjModule *objModule);

#endif // STOVE_PARSER_H
