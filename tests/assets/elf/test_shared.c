#include <stdio.h>

extern int add(int a, int b);

int main() {
    int result = add(40, 2);
    printf("add(40, 2) = %d\n", result);
    return result;
}
