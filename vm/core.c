//
// Created by asxe on 2024/1/26.
//

#include "core.h"
#include <string.h>
#include <sys/stat.h>
#include "../utils/utils.h"
#include "vm.h"
#include "../compiler/compiler.h"
#include "../objectAndClass/include/obj_thread.h"
#include "coreScript.inc"

char *rootDir = NULL; // 根目录

#define CORE_MODULE VT_TO_VALUE(VT_NULL)

// 返回值类型是Value类型，且是放在args[0]，args是Value类型数组
// RET_VALUE的参数是Value类型，无须转换直接赋值，它是“RET_”的基础
#define RET_VALUE(value) \
    do {                 \
        args[0] = value; \
        return true;     \
    } while (0);

// 将obj转换成Value后作为返回值
#define RET_OBJ(objPtr) RET_VALUE(OBJ_TO_VALUE(objPtr))

// 将布尔值转为Value后作为返回值
#define RET_BOOL(boolean) RET_VALUE(BOOL_TO_VALUE(boolean))
#define RET_NUM(num) RET_VALUE(NUM_TO_VALUE(num))
#define RET_NULL RET_VALUE(VT_TO_VALUE(VT_NULL))
#define RET_TRUE RET_VALUE(VT_TO_VALUE(VT_TRUE))
#define RET_FALSE RET_VALUE(VT_TO_VALUE(VT_FALSE))

// 设置线程报错
#define SET_ERROR_FALSE(vmPtr, errMsg)                                                          \
    do {                                                                                        \
        vmPtr->curThread->errorObj = OBJ_TO_VALUE(newObjString(vmPtr, errMsg, strlen(errMsg))); \
        return false;                                                                           \
    } while (0);

// 绑定方法func到classPtr指向的类
#define PRIM_METHOD_BIND(classPtr, methodName, func) {                                    \
        uint32_t length = strlen(methodName);                                             \
        int globalIdx = getIndexFromSymbolTable(&vm->allMethodNames, methodName, length); \
        if (globalIdx == -1)                                                              \
            globalIdx = addSymbol(vm, &vm->allMethodNames, methodName, length);           \
        Method method;                                                                    \
        method.methodType = MT_PRIMITIVE;                                                 \
        method.primFun = func;                                                            \
        bindMethod(vm, classPtr, (uint32_t) globalIdx, method);                           \
}

// 读取源代码文件
char *readFile(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        IO_ERROR("There was an error while opening the file : \"%s\"", path);

    struct stat fileStat;
    stat(path, &fileStat);
    size_t fileSize = fileStat.st_size;
    char *fileContent = (char *) malloc(fileSize + 1);
    if (fileContent == NULL)
        MEM_ERROR("Couldn't allocate memory for reading file : \"%s\"", path);

    size_t numRead = fread(fileContent, sizeof(char), fileSize, file);
    if (numRead < fileSize) {
        fclose(file);
        free(fileContent);
        IO_ERROR("Couldn't read file : \"%s\"", path);
    }
    fileContent[fileSize] = EOS;
    fclose(file);
    return fileContent;
}

//校验是否是函数
static bool validateFun(VM *vm, Value args) {
    if (VALUE_TO_OBJCLOSURE(args))
        return true;
    vm->curThread->errorObj = OBJ_TO_VALUE(newObjString(vm, "argument must be a function.", 28));
    return false;
}

//返回核心模块name的value结构
static Value getCoreClassValue(ObjModule *objModule, const char *name) {
    int index = getIndexFromSymbolTable(&objModule->moduleVarName, name, strlen(name));
    if (index == -1) {
        char id[MAX_ID_LEN] = {EOS};
        memcpy(id, name, strlen(name));
        RUN_ERROR("something wrong occur: missing core class \"%s\".", id);
    }
    return objModule->moduleVarValue.datas[index];
}

// !object: object取反，结果为false
static bool primObjectNot(VM *vm UNUSED, Value *args) {
    RET_VALUE(VT_TO_VALUE(VT_FALSE))
}

//args[0] == args[1]: 返回object是否相等
static bool primObjectEqual(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue)
}

//args[0] != args[1]: 返回object是否不等
static bool primObjectNotEqual(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(!valueIsEqual(args[0], args[1]));
    RET_VALUE(boolValue)
}

// args[0] is args[1]:类args[0]是否为类args[1]的子类
static bool primObjectIsSonClass(VM *vm, Value *args) {
    //args[1]必须是class
    if (!VALUE_IS_CLASS(args[1]))
        RUN_ERROR("argument must be class.");

    Class *thisClass = getClassOfObj(vm, args[0]);
    Class *baseClass = (Class *) (args[1].objHeader);

    //有可能是多级继承，因此自下而上遍历基类链
    while (baseClass != NULL) {
        //在某一级基类找到匹配，就设置返回值为VT_TRUE并返回
        if (thisClass == baseClass)
            RET_VALUE(VT_TO_VALUE(VT_TRUE))
        baseClass = baseClass->superClass;
    }

    //若未找到基类，说明不具备is_a关系
    RET_VALUE(VT_TO_VALUE(VT_FALSE))
}

//args[0].toString:返回args[0]所属class的名字
static bool primObjectToString(VM *vm UNUSED, Value *args) {
    Class *class = args[0].objHeader->class;
    Value nameValue = OBJ_TO_VALUE(class->name);
    RET_VALUE(nameValue)
}

//args[0].type:返回对象args[0]的类
static bool primObjectType(VM *vm, Value *args) {
    Class *class = getClassOfObj(vm, args[0]);
    RET_OBJ(class)
}

//args[0].name：返回类名
static bool primClassName(VM *vm UNUSED, Value *args) {
    RET_OBJ(VALUE_TO_CLASS(args[0])->name)
}

//args[0].superType:返回args[0]的基类
static bool primClassSuperType(VM *vm UNUSED, Value *args) {
    Class *class = VALUE_TO_CLASS(args[0]);
    if (class->superClass != NULL)
        RET_OBJ(class->superClass)
    RET_VALUE(VT_TO_VALUE(VT_NULL))
}

//args[0].toString:返回类名
static bool primClassToString(VM *vm UNUSED, Value *args) {
    RET_OBJ(VALUE_TO_CLASS(args[0])->name)
}

//args[0].same(args[1],args[2]):返回args[1]和args[2]是否相等
static bool primObjectMetaSame(VM *vm UNUSED, Value *args) {
    Value boolValue = BOOL_TO_VALUE(valueIsEqual(args[1], args[2]));
    RET_VALUE(boolValue)
}

//返回bool的字符串形式，true或false
static bool primBoolToString(VM *vm, Value *args) {
    ObjString *objString;
    if (VALUE_TO_BOOL(args[0])) //若为VT_TRUE
        objString = newObjString(vm, "true", 4);
    else
        objString = newObjString(vm, "false", 5);
    RET_OBJ(objString)
}

//bool值取反
static bool primBoolNot(VM *vm UNUSED, Value *args) {
    RET_BOOL(!VALUE_TO_BOOL(args[0]))
}

//Thread.new(func)：创建一个thread实例
static bool primThreadNew(VM *vm, Value *args) {
    //代码块为参数必为闭包
    if (!validateFun(vm, args[1]))
        return false;
    ObjThread *objThread = newObjThread(vm, VALUE_TO_OBJCLOSURE(args[1]));

    //使stack[0]为接收者，保持栈平衡
    objThread->stack[0] = VT_TO_VALUE(VT_NULL);
    objThread->esp++;
    RET_OBJ(objThread)
}

//Thread.abort(error)：以错误信息error为参数退出线程
static bool primThreadAbort(VM *vm, Value *args) {
    //此函数后续未处理
    vm->curThread->errorObj = args[1]; //保存退出参数
    return VALUE_IS_NULL(args[1]);
}

//Thread.current：返回当前的线程
static bool primThreadCurrent(VM *vm, Value *args UNUSED) {
    RET_OBJ(vm->curThread)
}

//Thread.suspend()：挂起线程，退出解析器
static bool primThreadSuspend(VM *vm, Value *args UNUSED) {
    //目前suspend操作只会退出虚拟机，使curThread为NULL，虚拟机将退出
    vm->curThread = NULL;
    return false;
}

//Thread.yield(arg)带参数让出CPU
static bool primThreadYieldWithArg(VM *vm, Value *args) {
    ObjThread *curThread = vm->curThread;
    vm->curThread = curThread->caller; //使CPU控制权回到主调方

    curThread->caller = NULL; //与调用者断开联系

    if (vm->curThread != NULL) {
        //如果当前线程有主调方，就将当前线程的返回值放在主调方的栈顶
        vm->curThread->esp[-1] = args[1];

        //对于thread.yield(arg)来说，回收arg的空间
        //保留thread参数所在的空间，将来唤醒时用于存储yield结果
        curThread->esp--;
    }
    return false;
}

//Thread.yield() 无参数让出CPU
static bool primThreadYieldWithoutArg(VM *vm, Value *args UNUSED) {
    ObjThread *curThread = vm->curThread;
    vm->curThread = curThread->caller; //使CPU控制权回到主调方

    curThread->caller = NULL; //与调用者断开联系

    if (vm->curThread != NULL)
        //为保持通用的栈结构，如果当前线程有主调方，就将空值作为返回值放在主调方的栈顶
        vm->curThread->esp[-1] = VT_TO_VALUE(VT_NULL);
    return false;
}

//切换到下一个线程nextThread
static bool switchThread(VM *vm, ObjThread *nextThread, Value *args, bool withArg) {
    //在下一个线程nextThread执行之前，其主调线程应该为空
    if (nextThread->caller != NULL)
        RUN_ERROR("thread has been called.");
    nextThread->caller = vm->curThread;

    if (nextThread->usedFrameNum == 0)
        //只有已经运行完毕的thread的usedFrameNum才为0
        SET_ERROR_FALSE(vm, "a finished thread can't be switched to.")
    if (!VALUE_IS_NULL(nextThread->errorObj))
        //Thread.abort(arg)会设置errorObj，不能切换到abort的线程
        SET_ERROR_FALSE(vm, "a aborted thread can't be switched to.")

    //如果call有参数，回收参数的空间，只保留此栈顶用于存储nextThread返回后的结果
    if (withArg)
        vm->curThread->esp--;

    ASSERT(nextThread->esp > nextThread->stack, "esp should be greater than stack.");
    //nextThread.call(arg)中的arg作为nextThread.yield的返回值
    //存储到nextThread的栈顶，否则压入null保持平衡
    nextThread->esp[-1] = withArg ? args[1] : VT_TO_VALUE(VT_NULL);

    //使当前线程指向nextThread，使之就绪
    vm->curThread = nextThread;

    //返回false以进入vm中的切换线程流
    return false;
}

//objThread.call()
static bool primThreadCallWithoutArg(VM *vm, Value *args) {
    return switchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, false);
}

//objThread.call(arg)
static bool primThreadCallWithArg(VM *vm, Value *args) {
    return switchThread(vm, VALUE_TO_OBJTHREAD(args[0]), args, true);
}

//objThread.isDone返回线程是否运行完成
static bool primThreadIsDone(VM *vm UNUSED, Value *args) {
    //获取.isDone的调用者
    ObjThread *objThread = VALUE_TO_OBJTHREAD(args[0]);
    RET_BOOL(objThread->usedFrameNum == 0 || !VALUE_IS_NULL(objThread->errorObj))
}

//从modules中获取名为moduleName的模块
static ObjModule *getModule(VM *vm, Value moduleName) {
    Value value = mapGet(vm->allModules, moduleName);
    if (value.type == VT_UNDEFINED)
        return NULL;
    return VALUE_TO_OBJMODULE(value);
}

//载入模块moduleName并编译
static ObjThread *loadModule(VM *vm, Value moduleName, const char *moduleCode) {
    //确保模块已经载入到vm->allModules
    //先查看是否已经导入了该模块，避免重新导入
    ObjModule *module = getModule(vm, moduleName);

    //若该模块未加载先将其载入，并继承核心模块中的变量
    if (module == NULL) {
        //创建模块并添加到vm->allModules中
        ObjString *newModuleName = VALUE_TO_OBJSTR(moduleName);
        ASSERT(newModuleName->value.start[newModuleName->value.length] == EOS, "string.value.start is not terminated.");
        module = newObjModule(vm, newModuleName->value.start);
        mapSet(vm, vm->allModules, moduleName, OBJ_TO_VALUE(module));

        //继承核心模块中的变量
        ObjModule *coreModule = getModule(vm, CORE_MODULE);
        uint32_t idx = 0;
        while (idx < coreModule->moduleVarName.count) {
            defineModuleVar(vm, module, coreModule->moduleVarName.datas[idx].str,
                            strlen(coreModule->moduleVarName.datas[idx].str), coreModule->moduleVarValue.datas[idx]);
            idx++;
        }
    }

    ObjFun *fun = compileModule(vm, module, moduleCode);
    ObjClosure *objClosure = newObjClosure(vm, fun);
    ObjThread *moduleThread = newObjThread(vm, objClosure);

    return moduleThread;
}

//table中查找符号symbol，找到后返回索引，否则返回-1
int getIndexFromSymbolTable(SymbolTable *table, const char *symbol, uint32_t length) {
    ASSERT(length != 0, "length of symbol is 0.");
    uint32_t index = 0;
    while (index < table->count) {
        if (length == table->datas[index].length && memcmp(table->datas[index].str, symbol, length) == 0)
            return (int) index;
        index++;
    }
    return -1;
}

//往table中添加符号symbol，返回其索引
int addSymbol(VM *vm, SymbolTable *table, const char *symbol, uint32_t length) {
    ASSERT(length != 0, "length of symbol is 0.");
    String string;
    string.str = ALLOCATE_ARRAY(vm, char, length + 1);
    memcpy(string.str, symbol, length);
    string.str[length] = EOS;
    string.length = length;
    StringBufferAdd(vm, table, string);
    return (int) (table->count - 1);
}

//确保符号已添加到符号表
int ensureSymbolExist(VM *vm, SymbolTable *table, const char *symbol, uint32_t length) {
    int symbolIndex = getIndexFromSymbolTable(table, symbol, length);
    if (symbolIndex == -1)
        return addSymbol(vm, table, symbol, length);
    return symbolIndex;
}

//定义类
static Class *defineClass(VM *vm, ObjModule *objModule, const char *name) {
    Class *class = newRawClass(vm, name, 0);
    //把类作为普通变量在模块中定义
    defineModuleVar(vm, objModule, name, strlen(name), OBJ_TO_VALUE(class));
    return class;
}

//使class->methods[index] = method
void bindMethod(VM *vm, Class *class, uint32_t index, Method method) {
    if (index >= class->methods.count) {
        Method emptyPad = {MT_NONE, {0}};
        MethodBufferFillWrite(vm, &class->methods, emptyPad, index - class->methods.count + 1);
    }
    class->methods.datas[index] = method;
}

//绑定基类
void bindSuperClass(VM *vm, Class *subClass, Class *superClass) {
    subClass->superClass = superClass;

    //继承基类属性数
    subClass->fieldNum += superClass->fieldNum;
    //继承基类方法
    uint32_t idx = 0;
    while (idx < superClass->methods.count) {
        bindMethod(vm, subClass, idx, superClass->methods.datas[idx]);
        idx++;
    }
}

// 执行模块
VMResult executeModule(VM *vm, Value moduleName, const char *moduleCode) {
    ObjThread *objThread = loadModule(vm, moduleName, moduleCode);
    return executeInstruction(vm, objThread);
}

// 编译核心模块
void buildCore(VM *vm) {
    // 创建核心模块，录入到vm->allModules
    ObjModule *coreModule = newObjModule(vm, NULL);
    mapSet(vm, vm->allModules, CORE_MODULE, OBJ_TO_VALUE(coreModule));

    //创建object类并绑定方法
    vm->objectClass = defineClass(vm, coreModule, "object");
    PRIM_METHOD_BIND(vm->objectClass, "!", primObjectNot)
    PRIM_METHOD_BIND(vm->objectClass, "==(_)", primObjectEqual)
    PRIM_METHOD_BIND(vm->objectClass, "!=(_)", primObjectNotEqual)
    PRIM_METHOD_BIND(vm->objectClass, "is(_)", primObjectIsSonClass)
    PRIM_METHOD_BIND(vm->objectClass, "toString", primObjectToString)
    PRIM_METHOD_BIND(vm->objectClass, "type", primObjectType)

    //定义classOfClass类，它是所有meta类的meta类和基类
    vm->classOfClass = defineClass(vm, coreModule, "class");
    //objectClass是任何类的基类
    bindSuperClass(vm, vm->classOfClass, vm->objectClass);

    PRIM_METHOD_BIND(vm->classOfClass, "name", primClassName)
    PRIM_METHOD_BIND(vm->classOfClass, "superType", primClassSuperType)
    PRIM_METHOD_BIND(vm->classOfClass, "toString", primClassToString)

    //定义object类的元信息类objectMetaClass，它不用挂载到vm
    Class *objectMetaClass = defineClass(vm, coreModule, "objectMeta");
    //classOfClass类是所有meta类的meta类和基类
    bindSuperClass(vm, objectMetaClass, vm->classOfClass);

    //类型比较
    PRIM_METHOD_BIND(objectMetaClass, "same(_,_)", primObjectMetaSame)
    //绑定各自的meta类
    vm->objectClass->objHeader.class = objectMetaClass;
    objectMetaClass->objHeader.class = vm->classOfClass;
    vm->classOfClass->objHeader.class = vm->classOfClass; //自己指向自己

    //执行核心模块
    executeModule(vm, CORE_MODULE, coreModuleCode);

    //Bool类定义在coreScript.inc文件中，将其挂载Bool类到vm->boolClass
    vm->boolClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Bool"));
    PRIM_METHOD_BIND(vm->boolClass, "toString", primBoolToString)
    PRIM_METHOD_BIND(vm->boolClass, "!", primBoolNot)

    //Thread类也是coreScript.inc中定义的，将其挂载到vm->threadClass并补充原生方法
    vm->threadClass = VALUE_TO_CLASS(getCoreClassValue(coreModule, "Thread"));
    //以下是类方法
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "new(_)", primThreadNew)
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "abort(_)", primThreadAbort)
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "current", primThreadCurrent)
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "suspend()", primThreadSuspend)
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield(_)", primThreadYieldWithArg)
    PRIM_METHOD_BIND(vm->threadClass->objHeader.class, "yield()", primThreadYieldWithoutArg)
    //以下是实例方法
    PRIM_METHOD_BIND(vm->threadClass, "call()", primThreadCallWithoutArg)
    PRIM_METHOD_BIND(vm->threadClass, "call(_)", primThreadCallWithArg)
    PRIM_METHOD_BIND(vm->threadClass, "isDone", primThreadIsDone)
}
