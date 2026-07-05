#include <stdio.h>

int add(int a, int b) {
    return a + b;
}

int main(void) {
    int result = add(2, 3);
    printf("result = %d\n", result);
    return 0;
}
