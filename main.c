#include <stdio.h>

int main() {
    FILE *file;
    char buffer[100];  // 用于存储读取的内容

    // 打开文件
    file = fopen("F:\\WorkSpace\\c-case\\stove\\test.🐔", "rb");

    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }

    // 读取文件内容
    size_t bytesRead;
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        // 在这里可以对读取的内容进行处理，比如输出到屏幕
        fwrite(buffer, 1, bytesRead, stdout);
    }

    if (ferror(file)) {
        perror("Error reading file");
        fclose(file);
        return 1;
    }

    // 关闭文件
    fclose(file);

    return 0;
}
