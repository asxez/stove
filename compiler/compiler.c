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

typedef enum {
    VAR_SCOPE_INVALID,
    VAR_SCOPE_LOCAL, //局部变量
    VAR_SCOPE_UPVALUE, //upvalue
    VAR_SCOPE_MODULE //模块变量
} VarScopeType; //标识变量作用域

typedef struct {
    VarScopeType scopeType; //变量的作用域
    int index;
} Variable;

//指示符函数指针
typedef void (*DenotationFun)(CompileUnit *cu, bool canAssign);

//签名函数指针
typedef void (*methodSignatureFun)(CompileUnit *cu, Signature *signature);

static uint32_t addConstant(CompileUnit *cu, Value constant);

static void expression(CompileUnit *cu, BindPower rbp);

static void compileProgram(CompileUnit *cu);

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

//添加局部变量到cu
static uint32_t addLocalVar(CompileUnit *cu, const char *name, uint32_t length) {
    LocalVar *var = &(cu->localVars[cu->localVarNum]);
    var->name = name;
    var->length = length;
    var->scopeDepth = cu->scopeDepth;
    var->isUpvalue = false;
    return cu->localVarNum++;
}

//声明局部变量
static int declareLocalVar(CompileUnit *cu, const char *name, uint32_t length) {
    if (cu->localVarNum >= MAX_LOCAL_VAR_NUM)
        COMPILE_ERROR(cu->curParser, "the max length of local variable of one scope %d", MAX_LOCAL_VAR_NUM);
    //判断当前作用域中该变量是否已存在
    int idx = (int) cu->localVarNum - 1;
    while (idx >= 0) {
        LocalVar *var = &cu->localVars[idx];
        //只在当前作用域中查找同名变量，如果找到了父作用域就退出，减少没必要的遍历
        if (var->scopeDepth < cu->scopeDepth)
            break;
        if (var->length == length && memcmp(var->name, name, length) == 0) {
            char id[MAX_ID_LEN] = {EOS};
            memcpy(id, name, length);
            COMPILE_ERROR(cu->curParser, "identifier \"%s\" redefinition.", id);
        }
        idx--;
    }
    //检查过后声明该局部变量
    return (int) addLocalVar(cu, name, length);
}

//根据作用域声明变量
static int declareVariable(CompileUnit *cu, const char *name, uint32_t length) {
    //若当前是模块作用域就声明为模块变量
    if (cu->scopeDepth == -1) {
        int index = defineModuleVar(cu->curParser->vm, cu->curParser->curModule, name, length, VT_TO_VALUE(VT_NULL));
        if (index == -1) { //重复定义报错
            char id[MAX_ID_LEN] = {EOS};
            memcpy(id, name, length);
            COMPILE_ERROR(cu->curParser, "identifier \"%s\" redefinition.", id);
        }
        return index;
    }
    //否则是局部作用域，声明局部变量
    return declareLocalVar(cu, name, length);
}

//声明模块变量，与defineModuleVar的区别是不做重定义检查，默认为声明
static int declareModuleVar(VM *vm, ObjModule *objModule, const char *name, uint32_t length, Value value) {
    ValueBufferAdd(vm, &objModule->moduleVarValue, value);
    return addSymbol(vm, &objModule->moduleVarName, name, length);
}

//返回包含cu->enclosingClassBK的最近CompileUnit
static CompileUnit *getEnclosingClassBKUnit(CompileUnit *cu) {
    while (cu != NULL) {
        if (cu->enclosingClassBK != NULL)
            return cu;
        cu = cu->enclosingUnit;
    }
    return NULL;
}

//返回包含cu最近的ClassBookKeep
static ClassBookKeep *getEnclosingClassBK(CompileUnit *cu) {
    CompileUnit *newCU = getEnclosingClassBKUnit(cu);
    if (newCU != NULL)
        return newCU->enclosingClassBK;
    return NULL;
}

//为实参列表中的各个实参生成加载实参的指令
static void processArgList(CompileUnit *cu, Signature *signature) {
    //由主调方保证参数不空
    ASSERT(cu->curParser->curToken.tokenType != TOKEN_RIGHT_PAREN &&
           cu->curParser->curToken.tokenType != TOKEN_RIGHT_BRACKET, "empty argument list.");
    do {
        if (++signature->argNum > MAX_ARG_NUM)
            COMPILE_ERROR(cu->curParser, "the max number of argument is %d.", MAX_ARG_NUM);
        expression(cu, BP_LOWEST);
    } while (matchToken(cu->curParser, TOKEN_COMMA));
}

//声明形参列表中的各个形参
static void processParaList(CompileUnit *cu, Signature *signature) {
    ASSERT(cu->curParser->curToken.tokenType != TOKEN_RIGHT_PAREN &&
           cu->curParser->curToken.tokenType != TOKEN_RIGHT_BRACKET, "empty argument list.");
    do {
        if (++signature->argNum > MAX_ARG_NUM)
            COMPILE_ERROR(cu->curParser, "the max number of argument is %d.", MAX_ARG_NUM);
        consumeCurToken(cu->curParser, TOKEN_ID, "expect variable name.");
        declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    } while (matchToken(cu->curParser, TOKEN_COMMA));
}

//尝试编译setter
static bool trySetter(CompileUnit *cu, Signature *signature) {
    if (!matchToken(cu->curParser, TOKEN_ASSIGN))
        return false;
    if (signature->signatureType == SIGN_SUBSCRIPT)
        signature->signatureType = SIGN_SUBSCRIPT_SETTER;
    else
        signature->signatureType = SIGN_SETTER;

    //读取等号右边的形参左边的(
    consumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after '='.");
    //读取形参
    consumeCurToken(cu->curParser, TOKEN_ID, "expect ID.");
    //声明形参
    declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    //读取等号右边的形参右边的(
    consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list.");
    signature->argNum++;
    return true;
}

//标识符的签名函数
static void idMethodSignature(CompileUnit *cu, Signature *signature) {
    signature->signatureType = SIGN_GETTER; //刚识别到id，默认为getter
    //new方法为构造函数
    if (signature->length == 3 && memcmp(signature->name, "new", 3) == 0) {
        //构造函数后面不能接=，即不能成为setter
        if (matchToken(cu->curParser, TOKEN_ASSIGN))
            COMPILE_ERROR(cu->curParser, "constructor shouldn't be setter.");
        //构造函数必须是标准的method，即new(_,...)，new后面必须接(
        if (!matchToken(cu->curParser, TOKEN_LEFT_PAREN))
            COMPILE_ERROR(cu->curParser, "constructor must be method.");
        signature->signatureType = SIGN_CONSTRUCT;
        //无参数就直接返回
        if (matchToken(cu->curParser, TOKEN_RIGHT_PAREN))
            return;
    } else { //若非构造函数
        if (trySetter(cu, signature)) //若是setter，此时已经将signatureType改为了setter，直接返回
            return;
        if (!matchToken(cu->curParser, TOKEN_LEFT_PAREN))
            //若后面没有(说明是getter，已在开头置为getter，直接返回
            return;
        //至此signatureType应该为一般形式的SIGN_METHOD，形式为name(paraList)
        signature->signatureType = SIGN_METHOD;
        //直接匹配到)，说明形参为空
        if (matchToken(cu->curParser, TOKEN_RIGHT_PAREN))
            return;
    }
    //处理形参
    processArgList(cu, signature);
    consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter list.");
}

//为单运算符方法创建签名
static void unaryMethodSignature(CompileUnit *cu UNUSED, Signature *signature UNUSED) {
    //名称部分在调用前已经完成，只修改类型
    signature->signatureType = SIGN_GETTER;
}

//为中缀运算符创建签名
static void infixMethodSignature(CompileUnit *cu, Signature *signature) {
    //在类中的运算符都是方法，类型为SIGN_METHOD
    signature->signatureType = SIGN_METHOD;

    //中缀运算符只有一个参数，故初始为1
    signature->argNum = 1;
    consumeCurToken(cu->curParser, TOKEN_LEFT_PAREN, "expect '(' after infix operator.");
    consumeCurToken(cu->curParser, TOKEN_ID, "expect variable name.");
    declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
    consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter.");
}

//为既做单运算符又做中缀运算符的符号方法创建签名
static void mixMethodSignature(CompileUnit *cu, Signature *signature) {
    //假设是单运算符方法，因此默认为getter
    signature->signatureType = SIGN_GETTER;

    //若后面有'('，说明其为中缀运算符，那就置其类型为SIGN_METHOD
    if (matchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        signature->signatureType = SIGN_METHOD;
        signature->argNum = 1;
        consumeCurToken(cu->curParser, TOKEN_ID, "expect variable name.");
        declareVariable(cu, cu->curParser->preToken.start, cu->curParser->preToken.length);
        consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after parameter.");
    }
}

//查找局部变量并返回索引
static int findLocal(CompileUnit *cu, const char *name, uint32_t length) {
    //内部作用域变量会覆外层，故从后往前，由最内层逐渐往外层找
    int index = cu->localVarNum - 1;
    while (index >= 0) {
        if (cu->localVars[index].length == length && memcmp(cu->localVars[index].name, name, length) == 0)
            return index;
        index--;
    }
    return -1;
}

//添加upvalue到cu->upvalues，返回其索引，若已经存在则只返回索引
static int addUpvalue(CompileUnit *cu, bool isEnclosingLocalVar, uint32_t index) {
    uint32_t idx = 0;
    while (idx < cu->fun->upvalueNum) {
        //如果该upvalue已经添加过了就返回其索引
        if (cu->upvalues[idx].index == index && cu->upvalues[idx].isEnclosingLocalVar == isEnclosingLocalVar)
            return (int) idx;
        idx++;
    }
    //若没有找到则将其添加
    cu->upvalues[cu->fun->upvalueNum].isEnclosingLocalVar = isEnclosingLocalVar;
    cu->upvalues[cu->fun->upvalueNum].index = index;
    return (int) cu->fun->upvalueNum++;
}

//查找name指代的upvalue后添加到cu->upvalues，返回其索引，否则返回-1
static int findUpvalue(CompileUnit *cu, const char *name, uint32_t length) {
    if (cu->enclosingUnit == NULL) //如果已经到了最外层仍未找到，返回-1
        return -1;
    //进入了方法的cu并且查找的不是静态域，即不是方法的Upvalue，那就没必要再往上找了
    if (!strchr(name, ' ') && cu->enclosingUnit->enclosingClassBK != NULL)
        return -1;
    //查看name是否为直接外层的局部变量
    int directOuterLocalIndex = findLocal(cu->enclosingUnit, name, length);

    //若是，将该外层局部变量置为upvalue
    if (directOuterLocalIndex != -1) {
        cu->enclosingUnit->localVars[directOuterLocalIndex].isUpvalue = true;
        return addUpvalue(cu, true, (uint32_t) directOuterLocalIndex);
    }
    //向外层递归查找
    int directOuterUpvalueIndex = findUpvalue(cu->enclosingUnit, name, length);
    if (directOuterUpvalueIndex != -1)
        return addUpvalue(cu, false, (uint32_t) directOuterUpvalueIndex);
    //执行到此说明没有该upvalue对应的局部变量，返回-1
    return -1;
}

//从局部变量和upvalue中查找符号name
static Variable getVarFromLocalOrUpvalue(CompileUnit *cu, const char *name, uint32_t length) {
    Variable var;
    //默认为无效作用域类型，查找到后会被更正
    var.scopeType = VAR_SCOPE_INVALID;
    var.index = findLocal(cu, name, length);
    if (var.index != -1) {
        var.scopeType = VAR_SCOPE_LOCAL;
        return var;
    }
    var.index = findUpvalue(cu, name, length);
    if (var.index != -1)
        var.scopeType = VAR_SCOPE_UPVALUE;
    return var;
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
