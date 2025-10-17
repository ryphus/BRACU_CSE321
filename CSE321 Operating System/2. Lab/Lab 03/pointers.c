#include <stdio.h>

void pointerBasics() {
    int a = 10;
    int *p = &a;

    printf("Value of a: %d\n", a);
    printf("Address of a: %p\n", &a);
    printf("Value stored in p (address of a): %p\n", p);
    printf("Value pointed to by p: %d\n", *p);
}

void pointerToArray() {
    int arr[] = {10, 20, 30};
    int *p = arr;

    for (int i = 0; i < 3; i++) {
        printf("arr[%d] = %d, *(p+%d) = %d\n", i, arr[i], i, *(p+i));
    }
}

void pointerToPointer() {
    int a = 42;
    int *p = &a;
    int **pp = &p;

    printf("a = %d, *p = %d, **pp = %d\n", a, *p, **pp);
}

void swap(int *x, int *y) {
    int temp = *x;
    *x = *y;
    *y = temp;
}

int main() {
    pointerBasics();

    printf("\n--- Pointer to Array ---\n");
    pointerToArray();

    printf("\n--- Pointer to Pointer ---\n");
    pointerToPointer();

    printf("\n--- Swapping using pointers ---\n");
    int a = 5, b = 10;
    printf("Before swap: a = %d, b = %d\n", a, b);
    swap(&a, &b);
    printf("After swap: a = %d, b = %d\n", a, b);

    return 0;
}

