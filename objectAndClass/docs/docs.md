## 核心模块
stove规定，核心模块名初始化为null，避免与用户自定义的模块名冲突。具体见include/meta_obj.c文件。

## upvalue, openUpvalue, closedUpvalue
upvalue即自由变量，是内部函数所引用的位于外层函数中的局部变量。

upvalue指向的外层函数局部变量若在外层函数的栈中，则该upvalue就被称为openUpvalue，若不在栈中，则被称为closedUpvalue。

## 字典对象
针对字典对象的删除，采用伪删除。

key的类型为undefined，则表示槽位（slot）空闲，如果此时value的类型为true，则表示探测链未断，可继续探测。
key的类型为undefined，则表示槽位空闲，若此时value类型为false，则表示探测链结束，探测结束。
