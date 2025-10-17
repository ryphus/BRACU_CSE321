#include <stdio.h>

// Function with no parameters
void greet() {
    printf("Hello from a function!\n");
}

// Function with parameters
void printSum(int x, int y) {
    printf("Sum = %d\n", x + y);
}

// Function taking array and its size as parameters
void printSumArr(int x[], int n) {
    int sum = 0;
    for (int i = 0; i < n; i++)
        sum += x[i];

    printf("Sum of array elements = %d\n", sum);
}

// Function with return value
int multiply(int a, int b) {
    return a * b;
}

// Recursive function
int factorial(int n) {
    if (n <= 1) return 1;
    return n * factorial(n - 1);
}

int main() {
    greet();

    printf("\n--- Function with parameters ---\n");
    printSum(3, 7);

    printf("\n--- Function with array parameter ---\n");
    int arr[] = {1, 2, 3, 4, 5};
    int size = sizeof(arr) / sizeof(arr[0]);
    printSumArr(arr, size);

    printf("\n--- Function with return value ---\n");
    int prod = multiply(4, 5);
    printf("Product = %d\n", prod);

    printf("\n--- Recursive Function (Factorial) ---\n");
    int n = 5;
    printf("Factorial of %d is %d\n", n, factorial(n));

    return 0;
}

