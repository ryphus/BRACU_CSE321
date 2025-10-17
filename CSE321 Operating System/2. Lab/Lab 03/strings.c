#include <stdio.h>
#include <string.h>  // Required for string functions

int main() {
    // Declare two strings
    char str1[100] = "Hello";
    char str2[100] = "World";

    // 1. strlen()
    printf("Length of str1: %lu\n", strlen(str1));  // %lu for size_t

    // 2. strcpy()
    char copy[100];
    strcpy(copy, str1);
    printf("Copied str1 to copy: %s\n", copy);

    // 3. strcat()
    strcat(str1, str2);  // str1 = "HelloWorld"
    printf("Concatenated str1 and str2: %s\n", str1);

    // Reset str1 and str2
    strcpy(str1, "Hello");
    strcpy(str2, "World");

    // 4. strcmp()
    int cmpResult = strcmp(str1, str2);
    if (cmpResult == 0) {
        printf("str1 and str2 are equal\n");
    } else if (cmpResult < 0) {
        printf("str1 is less than str2\n");
    } else {
        printf("str1 is greater than str2\n");
    }


    // 5. strchr() - find first occurrence of character
    char *ptr = strchr(str1, 'l');
    if (ptr != NULL) {
        printf("First occurrence of 'l' in str1 is at index: %ld\n", ptr - str1);
    }


    // 6. String input using scanf (no space)
    char name1[100];
    printf("Enter a word (no spaces): ");
    scanf("%s", name1);
    printf("You entered: %s", name1);

    // 7. String input using fgets (with space)
    char name2[100];
    printf("Enter a line (with spaces): ");
    fgets(name2, sizeof(name2), stdin);
    printf("You entered: %s", name2);

    return 0;
}

