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

static void infixOperator(CompileUnit *cu, bool canAssign UNUSED);

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

//生成把变量var加载到栈的指令
static void emitLoadVariable(CompileUnit *cu, Variable var) {
    switch (var.scopeType) {
        case VAR_SCOPE_LOCAL:
            //生成加载局部变量入栈的指令
            writeOpCodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, var.index);
            break;
        case VAR_SCOPE_UPVALUE:
            //生成加载upvalue到栈的指令
            writeOpCodeByteOperand(cu, OPCODE_LOAD_UPVALUE, var.index);
            break;
        case VAR_SCOPE_MODULE:
            //生成加载到模块变量到栈的指令
            writeOpCodeShortOperand(cu, OPCODE_LOAD_MODULE_VAR, var.index);
            break;
        default:
            NOT_REACHED()
    }
}

//为变量var生成存储的指令
static void emitStoreVariable(CompileUnit *cu, Variable var) {
    switch (var.scopeType) {
        case VAR_SCOPE_LOCAL:
            //生成存储到局部变量的指令
            writeOpCodeByteOperand(cu, OPCODE_STORE_LOCAL_VAR, var.index);
            break;
        case VAR_SCOPE_UPVALUE:
            //生成存储到upvalue的指令
            writeOpCodeByteOperand(cu, OPCODE_STORE_UPVALUE, var.index);
            break;
        case VAR_SCOPE_MODULE:
            //生成存储模块变量的指令
            writeOpCodeShortOperand(cu, OPCODE_STORE_MODULE_VAR, var.index);
            break;
        default:
            NOT_REACHED()
    }
}

//生成加载或存储变量的指令
static void emitLoadOrStoreVariable(CompileUnit *cu, bool canAssign, Variable var) {
    if (canAssign && matchToken(cu->curParser, TOKEN_ASSIGN)) {
        expression(cu, BP_LOWEST); //计算=右边表达式的值
        emitStoreVariable(cu, var); //为var生成赋值指令
    } else
        emitLoadVariable(cu, var); //生成加载指令
}

//生成把实例对象self加载到栈的指令
static void emitLoadSelf(CompileUnit *cu) {
    Variable var = getVarFromLocalOrUpvalue(cu, "self", 4);
    ASSERT(var.scopeType != VAR_SCOPE_INVALID, "get variable failed.");
    emitLoadVariable(cu, var);
}

//编译代码块
static void compileBlock(CompileUnit *cu) {
    //进入本函数前已经读入了{
    while (!matchToken(cu->curParser, TOKEN_RIGHT_BRACE)) {
        if (PEEK_TOKEN(cu->curParser) == TOKEN_EOF)
            COMPILE_ERROR(cu->curParser, "expect ')' at the end of block.");
        compileProgram(cu);
    }
}

//编译函数或者方法体
static void compileBody(CompileUnit *cu, bool isConstruct) {
    //进入本函数前已经读入了{
    compileBlock(cu);
    if (isConstruct)
        //若是构造函数就加载“self对象”作为下面OPCODE_RETURN的返回值
        writeOpCodeByteOperand(cu, OPCODE_LOAD_LOCAL_VAR, 0);
    else
        //否则加载null占位
        writeOpCode(cu, OPCODE_PUSH_NULL);
    //返回编译结果，若是构造函数就返回self，否则返回null
    writeOpCode(cu, OPCODE_RETURN);
}

//结束cu的编译工作，在其外层编译单元中为其创建闭包
#if DEBUG
static ObjFun *endCompileUnit(CompileUnit *cu, const char *debugName, uint32_t debugNameLen) {
    bindDebugFunName(cu->curParser->vm, cu->fun->debug, debugName, debugNameLen);
}
#else

static ObjFun *endCompileUnit(CompileUnit *cu) {
#endif
    //标识单元编译结束
    writeOpCode(cu, OPCODE_END);
    if (cu->enclosingUnit != NULL) {
        //把当前编译的objFun作为常量添加到父编译单元的常量表
        uint32_t index = addConstant(cu->enclosingUnit, OBJ_TO_VALUE(cu->fun));
        //内层函数以闭包形式在外层函数中存在，在外层函数的指令流中添加“为当前内层函数创建闭包的指令”
        writeOpCodeShortOperand(cu->enclosingUnit, OPCODE_CREATE_CLOSURE, index);

        //为vm在创建闭包时判断引用的是局部变量还是upvalue，下面为每个upvalue生成参数
        index = 0;
        while (index < cu->fun->upvalueNum) {
            writeByte(cu->enclosingUnit, cu->upvalues[index].isEnclosingLocalVar ? 1 : 0);
            writeByte(cu->enclosingUnit, cu->upvalues[index].index);
            index++;
        }
    }

    // /下面本编译单元，使当前编译单元指向外层编译单元
    cu->curParser->curCompileUnit = cu->enclosingUnit;
    return cu->fun;
}

//生成getter或者一般method调用指令
static void emitGetterMethodCall(CompileUnit *cu, Signature *signature, OpCode opCode) {
    Signature newSign;
    newSign.signatureType = SIGN_GETTER; //默认为getter，假设下面的两个if不执行
    newSign.name = signature->name;
    newSign.length = signature->length;
    newSign.argNum = 0;

    //如果是method，有可能有参数列表，在生成调用方法的指令前必须把参数入栈，否则运行方法时除了会获取到错误的参数（即栈中已有数据）外，gc还会在从方法返回时，错误的回收参数空间而导致失衡
    //下面调用的processArgList是把实参入栈，供方法使用
    if (matchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        newSign.signatureType = SIGN_METHOD;

        //若后面不是)，说明有参数列表
        if (!matchToken(cu->curParser, TOKEN_RIGHT_PAREN)) {
            processArgList(cu, &newSign);
            consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list.");
        }
    }

    //对method来说可能还传入了块参数
    if (matchToken(cu->curParser, TOKEN_LEFT_BRACE)) {
        newSign.argNum++;
        //进入本if时，上面的if块未必执行过，此时newSign.signatureType也许还是GETTER，下面要将其设置为METHOD
        newSign.signatureType = SIGN_METHOD;
        CompileUnit funCU;
        initCompileUnit(cu->curParser, &funCU, cu, false);

        Signature tmpFunSign = {SIGN_METHOD, "", 0, 0}; //临时用于编译函数
        if (matchToken(cu->curParser, TOKEN_BIT_OR)) { //若块参数也有参数
            processParaList(&funCU, &tmpFunSign); //将形参声明为函数的局部变量
            consumeCurToken(cu->curParser, TOKEN_BIT_OR, "expect '|' after argument list.");
        }
        funCU.fun->argNum = tmpFunSign.argNum;

        //编译函数体，将指令流写进该函数自己的指令单元funCU
        compileBody(&funCU, false);

#if DEBUG
        //以此函数被传给的方法来命名这个函数，函数名=方法名+" block arg"
        char funName[MAX_SIGN_LEN + 10] = {EOS};
        uint32_t len = signToString(&newSign, funName);
        memmove(funName + len, " block arg", 10);
        endCompileUnit(&funCU, funName, len + 10);
#else
        endCompileUnit(&funCU);
#endif
    }
    //如果是在构造函数中调用了super则会执行到此，构造函数中调用的方法只能是super
    if (signature->signatureType == SIGN_CONSTRUCT) {
        if (newSign.signatureType != SIGN_METHOD)
            COMPILE_ERROR(cu->curParser, "the form of supercall is super() or super(arguments).");
        newSign.signatureType = SIGN_CONSTRUCT;
    }
    //根据签名生成调用指令，如果上面的三个if都未执行，此处就是getter调用
    emitCallBySignature(cu, &newSign, opCode);
}

//生成方法调用指令，包括getter和setter
static void emitMethodCall(CompileUnit *cu, const char *name, uint32_t length, OpCode opCode, bool canAssign) {
    Signature signature;
    signature.signatureType = SIGN_GETTER;
    signature.name = name;
    signature.length = length;

    //若是setter则生成调用setter的指令
    if (matchToken(cu->curParser, TOKEN_ASSIGN) && canAssign) {
        signature.signatureType = SIGN_SETTER;
        signature.argNum = 1; //setter只接受一个参数

        //载入实参（即=右边所赋的值），为下面方法调用传参
        expression(cu, BP_LOWEST);
        emitCallBySignature(cu, &signature, opCode);
    } else
        emitGetterMethodCall(cu, &signature, opCode);
}

//生成加载类的指令
static void emitLoadModuleVar(CompileUnit *cu, const char *name) {
    int index = getIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, name, strlen(name));
    ASSERT(index != -1, "symbol should have been defined.");
    writeOpCodeShortOperand(cu, OPCODE_LOAD_MODULE_VAR, index);
}

//内嵌表达式.nud()
static void stringInterpolation(CompileUnit *cu, bool canAssign UNUSED) {
    // a % (b + c) d % (e) f
    //会按照一下形式编译
    //["a", b + c, " d ", e, "f "].join()
    //其中a和d都是TOKEN_INTERPOLATION，b c e都是TOKEN_ID，f是TOKEN_STRING

    //创造一个list实例，拆分字符串，将拆分出的各部分作为元素添加到list
    emitLoadModuleVar(cu, "list");
    emitCall(cu, 0, "new()", 5);

    //每次处理字符串中的一个内嵌表达式，包括两部分，以a % (b + c)为例：
    //1 加载TOKEN_INTERPOLATION对应的字符串，如a，将其添加到list
    //2 解析内嵌表达式，如b + c，将其结果添加到list
    do {
        //1 处理TOKEN_INTERPOLATION中的字符串，如a % (b + c)中的a
        literal(cu, false);
        //将字符串添加到list
        emitCall(cu, 1, "addCore_(_)", 11); //以_结尾的方法名是内部使用

        //2 解析内嵌表达式，如a % (b + c)中的b + c
        expression(cu, BP_LOWEST);
        //将结果添加到list
        emitCall(cu, 1, "addCore_(_)", 11);
    } while (matchToken(cu->curParser, TOKEN_INTERPOLATION));
    //处理下一个内嵌表达式，如a % (b + c) d % (e) f中的d % (e)

    //读取最后的字符串，a % (b + c) d % (e) f中的f
    consumeCurToken(cu->curParser, TOKEN_STRING, "expect string at the end of interpolation.");

    //加载最后的字符串
    literal(cu, false);

    //将字符串添加到list
    emitCall(cu, 1, "addCore_(_)", 11);

    //最后将以上list中的元素join为一个字符串
    emitCall(cu, 0, "join()", 6);
}

//编译bool
static void boolean(CompileUnit *cu, bool canAssign UNUSED) {
    //true和false的nud方法
    OpCode opCode = cu->curParser->preToken.tokenType == TOKEN_TRUE ? OPCODE_PUSH_TRUE : OPCODE_PUSH_FALSE;
    writeOpCode(cu, opCode);
}

//生成OPCODE_PUSH_NULL指令
static void null(CompileUnit *cu, bool canAssign UNUSED) {
    writeOpCode(cu, OPCODE_PUSH_NULL);
}

//"self".nud()
static void self(CompileUnit *cu, bool canAssign UNUSED) {
    if (getEnclosingClassBK(cu) == NULL)
        COMPILE_ERROR(cu->curParser, "self must be inside a class method.");
    emitLoadSelf(cu);
}

//"super".nud()
static void super(CompileUnit *cu, bool canAssign) {
    ClassBookKeep *enclosingClassBK = getEnclosingClassBK(cu);
    if (enclosingClassBK == NULL)
        COMPILE_ERROR(cu->curParser, "can't invoke super outside a class method.");

    //此处加载self，是保证参数args[0]始终是self对象，尽管对基类调用无用
    emitLoadSelf(cu);

    //判断形式super.methodname()
    if (matchToken(cu->curParser, TOKEN_DOT)) {
        consumeCurToken(cu->curParser, TOKEN_ID, "expect name after '.'");
        emitMethodCall(cu, cu->curParser->preToken.start, cu->curParser->preToken.length, OPCODE_SUPER0, canAssign);
    } else
        //super()：调用基类中与关键字super所在子类方法同名的方法
        emitGetterMethodCall(cu, enclosingClassBK->signature, OPCODE_SUPER0);
}

//小写字符开头便是局部变量
static bool isLocalName(const char *name) {
    return (bool) (name[0] >= 'a' && name[0] <= 'z');
}

//标识符.nud()：变量名或方法名
static void id(CompileUnit *cu, bool canAssign) {
    //备份变量名
    Token name = cu->curParser->preToken;
    ClassBookKeep *classBK = getEnclosingClassBK(cu);

    //标识符可以是任意符号，按照此顺序处理
    //函数调用->局部变量和Upvalue->实例域->静态域->类getter方法调用->模块变量

    //处理函数调用
    if (cu->enclosingUnit == NULL && matchToken(cu->curParser, TOKEN_LEFT_PAREN)) {
        char id[MAX_ID_LEN] = {EOS};
        //函数名加上"Fun"前缀作为模块变量名，检查前面是否已有此函数的定义
        memmove(id, "Fun ", 4);
        memmove(id + 4, name.start, name.length);

        Variable var;
        var.scopeType = VAR_SCOPE_MODULE;
        var.index = getIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, id, strlen(id));
        if (var.index == -1) {
            memmove(id, name.start, name.length);
            id[name.length] = EOS;
            COMPILE_ERROR(cu->curParser, "undefined function: '%s'.", id);
        }
        //1.把模块变量即函数闭包加载到栈
        emitLoadVariable(cu, var);

        Signature signature;
        //函数调用的形式和method类似，只不过method有一个可选的块参数
        signature.signatureType = SIGN_METHOD;
        //把函数调用编译为“闭包.call”的形式，故name为call
        signature.name = "call";
        signature.length = 4;
        signature.argNum = 0;

        //若后面不是)，说明有参数列表
        if (!matchToken(cu->curParser, TOKEN_RIGHT_PAREN)) {
            //2.压入实参
            processArgList(cu, &signature);
            consumeCurToken(cu->curParser, TOKEN_RIGHT_PAREN, "expect ')' after argument list.");
        }
        //3.生成调用指令以调用函数
        emitCallBySignature(cu, &signature, OPCODE_CALL0);
    } else { //否则按照各种变量来处理
        //按照局部变量和upvalue来处理
        Variable var = getVarFromLocalOrUpvalue(cu, name.start, name.length);
        if (var.index != -1) {
            emitLoadOrStoreVariable(cu, canAssign, var);
            return;
        }

        //按照实例域来处理
        if (classBK != NULL) {
            int fieldIndex = getIndexFromSymbolTable(&classBK->fields, name.start, name.length);
            if (fieldIndex != -1) {
                bool isRead = true;
                if (canAssign && matchToken(cu->curParser, TOKEN_ASSIGN)) {
                    isRead = false;
                    expression(cu, BP_LOWEST);
                }

                //如果当前正在编译类方法，则直接在该实例对象中加载field
                if (cu->enclosingUnit != NULL)
                    writeOpCodeByteOperand(cu, isRead ? OPCODE_LOAD_SELF_FIELD : OPCODE_STORE_SELF_FIELD, fieldIndex);
                else {
                    emitLoadSelf(cu);
                    writeOpCodeByteOperand(cu, isRead ? OPCODE_LOAD_FIELD : OPCODE_STORE_FIELD, fieldIndex);
                }
                return;
            }
        }

        //按照静态域查找
        if (classBK != NULL) {
            char *staticFieldId = ALLOCATE_ARRAY(cu->curParser->vm, char, MAX_ID_LEN);
            memset(staticFieldId, 0, MAX_ID_LEN);
            uint32_t staticFieldIdLen;
            char *clsName = classBK->name->value.start;
            uint32_t clsLen = classBK->name->value.length;

            //各类中静态域的名称以"Cls类名 静态域名"来命名
            memmove(staticFieldId, "Cls", 3);
            memmove(staticFieldId + 3, clsName, clsLen);
            memmove(staticFieldId + 3 + clsLen, " ", 1);
            const char *tkName = name.start;
            uint32_t tkLen = name.length;
            memmove(staticFieldId + 4 + clsLen, tkName, tkLen);
            staticFieldIdLen = strlen(staticFieldId);
            var = getVarFromLocalOrUpvalue(cu, staticFieldId, staticFieldIdLen);

            DEALLOCATE_ARRAY(cu->curParser->vm, staticFieldId, MAX_ID_LEN);
            if (var.index != -1) {
                emitLoadOrStoreVariable(cu, canAssign, var);
                return;
            }
        }

        //如果以上未找到同名变量，有可能是该标识符是同类中的其他方法调用
        //方法规定以小写字符开头
        if (classBK != NULL && isLocalName(name.start)) {
            emitLoadSelf(cu); //确保args[0]是self对象，以便查找到方法
            //因为类可能尚未编译完，未统计完所有方法，故此时无法判断方法是否未定义，留待运行时检测
            emitMethodCall(cu, name.start, name.length, OPCODE_CALL0, canAssign);
            return;
        }

        //按照模块变量处理
        var.scopeType = VAR_SCOPE_MODULE;
        var.index = getIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, name.start, name.length);
        if (var.index == -1) {
            //模块变量属于模块作用域，若当前引用处之前未定义该模块变量，说不定在后面有其定义，因此暂时先声明它，待模块统计完后再检查
            //用关键字fun定义的函数是以前缀Fun后接函数名作为模块变量，下面加上Fun前缀按照函数名重新查找
            char funName[MAX_SIGN_LEN + 5] = {EOS};
            memmove(funName, "Fun ", 4);
            memmove(funName + 4, name.start, name.length);
            var.index = getIndexFromSymbolTable(&cu->curParser->curModule->moduleVarName, funName, strlen(funName));

            //若不是函数名，那么可能是该模块变量定义在引用处的后面，先将行号作为该变量值去声明
            if (var.index == -1)
                var.index = declareModuleVar(cu->curParser->vm, cu->curParser->curModule, name.start, name.length,
                                             NUM_TO_VALUE(cu->curParser->curToken.lineNo));
        }
        emitLoadOrStoreVariable(cu, canAssign, var);
    }
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
        {NULL, BP_LOWEST, id, NULL, idMethodSignature}, //TOKEN_ID
        PREFIX_SYMBOL(stringInterpolation), //TOKEN_INTERPOLATION
        UNUSED_RULE, //TOKEN_VAR
        UNUSED_RULE, //TOKEN_FUN
        UNUSED_RULE, //TOKEN_IF
        UNUSED_RULE, //TOKEN_ELSE
        PREFIX_SYMBOL(boolean), //TOKEN_TRUE
        PREFIX_SYMBOL(boolean), //TOKEN_FALSE
        UNUSED_RULE, //TOKEN_WHILE
        UNUSED_RULE, //TOKEN_FOR
        UNUSED_RULE, //TOKEN_BREAK
        UNUSED_RULE, //TOKEN_CONTINUE
        UNUSED_RULE, //TOKEN_RETURN
        PREFIX_SYMBOL(null), //TOKEN_NULL
        UNUSED_RULE, //TOKEN_CLASS
        PREFIX_SYMBOL(self), //TOKEN_SELF
        UNUSED_RULE, //TOKEN_STATIC
        INFIX_OPERATOR("is", BP_IS), //TOKEN_IS
        PREFIX_SYMBOL(super), //TOKEN_SUPER
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
