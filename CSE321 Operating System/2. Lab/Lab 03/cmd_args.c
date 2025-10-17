#include <stdio.h>

int main(int argc, char *argv[]) {
    printf("Total arguments: %d\n", argc);

    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }

    // Example: Add two numbers from command line
    if (argc == 3) {
        int x = atoi(argv[1]);
        int y = atoi(argv[2]);
        printf("Sum = %d\n", x + y);
    } else {
        printf("To add two numbers, run like: ./cmdargs 5 10\n");
    }

    return 0;
}

