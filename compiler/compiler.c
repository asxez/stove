//
// Created by asxe on 2024/2/19.
//

#include "compiler.h"
#include "../lexicalParser/include/parser.h"
#include "../vm/core.h"
#include <string.h>
#include "../objectAndClass/include/class.h"

#if DEBUG
#include "debug.h"
#endif

struct compileUnit {
    ObjFun *fun; //所编译的函数
    LocalVar localVars[MAX_LOCAL_VAR_NUM]; //作用域中允许的局部变量的个数上限
    uint32_t localVarNum; //已分配的局部变量个数
    Upvalue upvalues[MAX_UPVALUE_NUM]; //记录本层函数所引用的upvalue
    int scopeDepth; //此项表示当前正在编译的代码所处的作用域
    uint32_t stackSlotNum; //当前使用的slot个数
    Loop *curLoop; //当前正在编译的循环层
    ClassBookKeep *enclosingClassBK; //当前正在编译的类的编译信息
    struct compileUnit *enclosingUnit; //包含此编译单元的编译单元，即直接外层
    Parser *curParser; //当前parser
};

//把opcode定义到数组opCodeSlotsUsed中
#define OPCODE_SLOTS(opcode, effect) effect,
static const int opCodeSlotsUsed[] = {
#include "../vm/opcode.inc"
};
#undef OPCODE_SLOTS

typedef enum {
    BP_NONE, //无绑定能力
    BP_LOWEST, //最低绑定能力
    BP_ASSIGN, // =
    BP_CONDITION, // ?:
    BP_LOGIC_OR, // ||
    BP_LOGIC_AND, // &&
    BP_EQUAL, // == !=
    BP_IS, // is
    BP_CMP, // < > <= >=
    BP_BIT_OR, // |
    BP_BIT_AND, // &
    BP_BIT_SHIFT, // >> <<
    BP_RANGE, // ..
    BP_TERM, // + -
    BP_FACTOR, // * / %
    BP_UNARY, // - ! ~
    BP_CALL, // . () []
    BP_HIGHEST //最高绑定能力
} BindPower; //操作符绑定权值

//指示符函数指针
typedef void (*DenotationFun)(CompileUnit *cu, bool canAssign);

//签名函数指针
typedef void (*methodSignatureFun)(CompileUnit *cu, Signature *signature);

static uint32_t addConstant(CompileUnit *cu, Value constant);

typedef struct {
    const char *id; //符号
    BindPower lbp; //左绑定权值
    DenotationFun nud; //字面量，变量，前缀运算符等不关注左操作数的Token调用的方法
    DenotationFun led; //中缀运算符等关注左操作数的Token调用的方法
    methodSignatureFun methodSignature; //表示本符号在类中被视为一个方法，为其生成一个方法签名
} SymbolBindRule; //符号绑定规则

//初始化CompileUnit
static void initCompileUnit(Parser *parser, CompileUnit *cu, CompileUnit *enclosingUnit, bool isMethod) {
    parser->curCompileUnit = cu;
    cu->curParser = parser;
    cu->enclosingUnit = enclosingUnit;
    cu->curLoop = NULL;
    cu->enclosingClassBK = NULL;

    //若无外层，说明当前属于模块作用域
    if (enclosingUnit == NULL) {
        //编译代码时是从上到下从最外层的模块作用域开始，模块作用域设为-1
        cu->scopeDepth = -1;
        //模块级作用域中没有局部变量
        cu->localVarNum = 0;
    } else { //若是内层单元，属于局部作用域
        if (isMethod) { //若是类中的方法
            //若是类的方法就设定隐式self为第0个局部变量，即实例对象，它是方法（消息）的接收者，self这种特殊对象被处理为局部变量
            cu->localVars[0].name = "self";
            cu->localVars[0].length = 4;
        } else { //普通函数
            //空出第0个局部变量，保持统一
            cu->localVars[0].name = NULL;
            cu->localVars[0].length = 0;
        }

        //第0个局部变量的特殊性使其作用域为模块级别
        cu->localVars[0].scopeDepth = -1;
        cu->localVars[0].isUpvalue = false;
        cu->localVarNum = 1; //localVars[0]被分配
        //对于函数和方法来说，初始作用域就是局部作用域
        //0表示局部作用域的最外层
        cu->scopeDepth = 0;
    }

    //局部变量保存在栈中，初始时栈中已使用的slot数量等于局部变量的数量
    cu->stackSlotNum = cu->localVarNum;
    cu->fun = newObjFun(cu->curParser->vm, cu->curParser->curModule, cu->localVarNum);
}

//往函数的指令流中写入1个字节，返回其索引
static int writeByte(CompileUnit *cu, int byte) {
    //若在调试状态，额外在debug->lineNo中写入当前token行号
#if DEBUG
    IntBufferAdd(cu->curParser->vm, &cu->fun->debug->lineNo, cu->curParser->preToken.lineNo);
#endif
    ByteBufferAdd(cu->curParser->vm, &cu->fun->instrStream, (uint8_t) byte);
    return (int) (cu->fun->instrStream.count - 1);
}

//写入操作码
static void writeOpCode(CompileUnit *cu, OpCode opCode) {
    writeByte(cu, opCode);
    //累计需要的运行时空间大小
    cu->stackSlotNum += opCodeSlotsUsed[opCode];
    if (cu->stackSlotNum > cu->fun->maxStackSlotUsedNum)
        cu->fun->maxStackSlotUsedNum = cu->stackSlotNum;
}

//写入1个字节的操作数
static int writeByteOperand(CompileUnit *cu, int operand) {
    return writeByte(cu, operand);
}

//写入两个字节的操作数，按大端字节序写入参数，低地址写高位，高地址写低位
static void writeShortOperand(CompileUnit *cu, int operand) {
    writeByte(cu, (operand >> 8) & 0xff); //写高8位
    writeByte(cu, operand & 0xff); //写低8位
}

//写入操作数为1字节大小的指令
static int writeOpCodeByteOperand(CompileUnit *cu, OpCode opCode, int operand) {
    writeOpCode(cu, opCode);
    return writeByteOperand(cu, operand);
}

//写入操作数为2字节大小的指令
static void writeOpCodeShortOperand(CompileUnit *cu, OpCode opCode, int operand) {
    writeOpCode(cu, opCode);
    writeShortOperand(cu, operand);
}

//添加常量并返回其索引
static uint32_t addConstant(CompileUnit *cu, Value constant) {
    ValueBufferAdd(cu->curParser->vm, &cu->fun->constants, constant);
    return cu->fun->constants.count - 1;
}

//把Signature转换为字符串，返回字符串长度
static uint32_t signToString(Signature *signature, char *buf) {
    uint32_t pos = 0;
    //复制方法名xxx
    memcpy(buf + pos, signature->name, signature->length);
    pos += signature->length;

    //下面单独处理方法名之后的部分
    switch (signature->signatureType) {

        //SIGN_GETTER形式：xxx，无参数，上面memcpy已完成
        case SIGN_GETTER:
            break;

        case SIGN_SETTER: //SING_SETTER:xxx=(_)，之前已完成xxx
            buf[pos++] = '=';
            //接下来添加=右边的赋值
            buf[pos++] = '(';
            buf[pos++] = '_';
            buf[pos++] = ')';
            break;

        case SIGN_CONSTRUCT: //SING_METHOD和SIGN_CONSTRUCT：xxx(_,...)
        case SIGN_METHOD: {
            buf[pos++] = '(';
            uint32_t idx = 0;
            while (idx < signature->argNum) {
                buf[pos++] = '_';
                buf[pos++] = ',';
                idx++;
            }
            if (idx == 0) //无参数
                buf[pos++] = ')';
            else
                buf[pos - 1] = ')';
            break;
        }

            //SIGN_SUBSCRIPT:xxx[_,...]
        case SIGN_SUBSCRIPT: {
            buf[pos++] = '[';
            uint32_t idx = 0;
            while (idx < signature->argNum) {
                buf[pos++] = '_';
                buf[pos++] = ',';
                idx++;
            }
            if (idx == 0)
                buf[pos++] = ']';
            else
                buf[pos - 1] = ']';
            break;
        }

            //SIGN_SUBSCRIPT_SETTER:xxx[_,...] = (_)
        case SIGN_SUBSCRIPT_SETTER: {
            buf[pos++] = '[';
            uint32_t idx = 0;
            //argNum包括了等号右边的一个赋值参数，这里是在处理等号左边subscript中的参数列表，因此减1.后面专门添加该参数
            while (idx < signature->argNum - 1) {
                buf[pos++] = '_';
                buf[pos++] = ',';
                idx++;
            }
            if (idx == 0)
                buf[pos++] = ']';
            else
                buf[pos - 1] = ']';

            //下面为等号右边的参数构造签名部分
            buf[pos++] = '=';
            buf[pos++] = '(';
            buf[pos++] = '_';
            buf[pos++] = ')';
            break;
        }
    }
    buf[pos] = EOS;
    return pos; //签名串长度
}

//生成加载常量的指令
static void emitLoadConstant(CompileUnit *cu, Value value) {
    int index = addConstant(cu, value);
    writeOpCodeShortOperand(cu, OPCODE_LOAD_CONSTANT, index);
}

//数字和字符串.nud() 编译字面量
static void literal(CompileUnit *cu, bool canAssign UNUSED) {
    //literal是常量（数字和字符串）的nud方法，用来返回字面值
    emitLoadConstant(cu, cu->curParser->preToken.value);
}

//不关注左操作数的符号称为前缀符号
//用于如字面量、变量名、前缀符号等非运算符
#define PREFIX_SYMBOL(nud) {NULL, BP_NONE, nud, NULL, NULL}

//前缀运算符，如!
#define PREFIX_OPERATOR(id) {id, BP_NONE, unaryOperator, NULL, unaryMethodSignature}

//关注左操作数的符号为中缀符号
//数组[，函数(，实例与方法之间的.等
#define INFIX_SYMBOL(lbp, led) {NULL, lbp, NULL, led, NULL}

//中缀运算符
#define INFIX_OPERATOR(id, lbp) {id, lbp, NULL, infixOperator, infixMethodSignature}

//既可做前缀又可以做中缀的运算符，例如-
#define MIX_OPERATOR(id) {id, BP_TERM, unaryOperator, infixOperator, mixMethodSignature}

//占位符
#define UNUSED_RULE {NULL, BP_NONE, NULL, NULL, NULL}

SymbolBindRule Rules[] = {
        UNUSED_RULE, //TOKEN_INVALID
        PREFIX_SYMBOL(literal), //TOKEN_NUM
        PREFIX_SYMBOL(literal), //TOKEN_STRING
};

//语法分析核心
static void expression(CompileUnit *cu, BindPower rbp) {
    //以中缀运算符表达式aSwTe为例，大写字符表示运算符，小写表示操作数
    //进入expression时，curToken为操作数w，preToken是运算符S
    DenotationFun nud = Rules[cu->curParser->curToken.tokenType].nud;

    //表达式开头的要么是操作数要么是前缀运算符，必然有nud方法
    ASSERT(nud != NULL, "nud is NULL.");
    getNextToken(cu->curParser); //执行后curToken为运算符T
    bool canAssign = rbp < BP_ASSIGN;
    nud(cu, canAssign); //计算操作数w的值

    while (rbp < Rules[cu->curParser->curToken.tokenType].lbp) {
        DenotationFun led = Rules[cu->curParser->curToken.tokenType].led;
        getNextToken(cu->curParser); //执行后curToken为e
        led(cu, canAssign); //计算运算符T.led方法
    }
}

//通过签名编译方法调用，包括callX和superX指令
static void emitCallBySignature(CompileUnit *cu, Signature *signature, OpCode opCode) {
    char signBuffer[MAX_SIGN_LEN];
    uint32_t length = signToString(signature, signBuffer);
    //确保签名录入到vm->allMethodNames中
    int symbolIndex = ensureSymbolExist(cu->curParser->vm, &cu->curParser->vm->allMethodNames, signBuffer, length);
    writeOpCodeShortOperand(cu, opCode + signature->argNum, symbolIndex);

    //此时在常量表中预创建一个空slot占位，将来绑定方法时再装入基类
    if (opCode == OPCODE_SUPER0)
        writeShortOperand(cu, addConstant(cu, VT_TO_VALUE(VT_NULL)));
}

//生成方法调用的指令，仅限callX指令
static void emitCall(CompileUnit *cu, int numArgs, const char *name, int length) {
    int symbolIndex = ensureSymbolExist(cu->curParser->vm, &cu->curParser->vm->allMethodNames, name, length);
    writeOpCodeShortOperand(cu, OPCODE_CALL0 + numArgs, symbolIndex);
}

//中缀运算符.led方法
static void infixOperator(CompileUnit *cu, bool canAssign UNUSED) {
    SymbolBindRule *rule = &Rules[cu->curParser->preToken.tokenType];

    //中缀运算符对左右操作数的绑定权值一样
    BindPower rbp = rule->lbp;
    expression(cu, rbp); //解析右操作数

    //生成一个参数的签名
    Signature signature = {SIGN_METHOD, rule->id, strlen(rule->id), 1};
    emitCallBySignature(cu, &signature, OPCODE_CALL0);
}

//前缀运算符.nud方法，-,!等
static void unaryOperator(CompileUnit *cu, bool canAssign UNUSED) {
    SymbolBindRule *rule = &Rules[cu->curParser->preToken.tokenType];

    //BP_UNARY作为rbp去调用expression解析右操作数
    expression(cu, BP_UNARY);

    //生成调用前缀运算符的指令
    //0个参数，前缀运算符都是1个字符，长度为1
    emitCall(cu, 0, rule->id, 1);
}

//在模块objModule中定义名为name，值为value的模块变量
int defineModuleVar(VM *vm, ObjModule *objModule, const char *name, uint32_t length, Value value) {
    if (length > MAX_ID_LEN) {
        //也许name指向的变量名并不以\0结束，将其从源码串中拷贝出来
        char id[MAX_ID_LEN] = {EOS};
        memcpy(id, name, length);

        //本函数可能是在编译源码文件之前调用的，那时还没有创建parser，因此报错要分情况
        if (vm->curParser != NULL)
            COMPILE_ERROR(vm->curParser, "length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
        else
            MEM_ERROR("length of identifier \"%s\" should be no more than %d", id, MAX_ID_LEN);
    }

    //从模块变量名中查找变量，若不存在就添加
    int symbolIndex = getIndexFromSymbolTable(&objModule->moduleVarName, name, length);
    if (symbolIndex == -1) {
        symbolIndex = addSymbol(vm, &objModule->moduleVarName, name, length); //添加变量名
        ValueBufferAdd(vm, &objModule->moduleVarValue, value); //添加变量值
    } else if (VALUE_IS_NUM(objModule->moduleVarValue.datas[symbolIndex]))
        //若遇到之前预先声明的模块变量的定义，在此为其赋予正确的值
        objModule->moduleVarValue.datas[symbolIndex] = value;
    else
        symbolIndex = -1;

    return symbolIndex;
}

//编译程序
static void compileProgram(CompileUnit *cu) {
    ;
}

//编译模块
ObjFun *compileModule(VM *vm, ObjModule *objModule, const char *moduleCode) {
    //各源码模块文件需要单独的parser
    Parser parser;
    parser.parent = vm->curParser;
    vm->curParser = &parser;

    if (objModule->name == NULL)
        //核心模块是coreScript.inc
        initParser(vm, &parser, "coreScript.inc", moduleCode, objModule);
    else
        initParser(vm, &parser, (const char *) objModule->name->value.start, moduleCode, objModule);

    CompileUnit moduleCU;
    initCompileUnit(&parser, &moduleCU, NULL, false);

    //记录现在模块变量的数量，后面检查预定义模块变量时可减少遍历
    uint32_t moduleVarNumBefor = objModule->moduleVarValue.count;
    //初始的parser->curToken.type为TOKEN_UNKNOW，下面使其指向第一个合法的token
    getNextToken(&parser);

    //此时compileProgram为桩函数，并不会读进token，死循环
    while (!matchToken(&parser, TOKEN_EOF))
        compileProgram(&moduleCU);

    printf("");
    exit(0);
}
