#include <stdio.h>

void writeToFile() {
    FILE *fp = fopen("demo.txt", "w");
    if (fp == NULL) {
        printf("Error opening file!\n");
        return;
    }

    fprintf(fp, "Hello File!\n");
    fprintf(fp, "Writing some data...\n");

    fclose(fp);
    printf("Data written to file.\n");
}

void readFromFile() {
    char buffer[100];
    FILE *fp = fopen("demo.txt", "r");
    if (fp == NULL) {
        printf("Error opening file for reading!\n");
        return;
    }

    printf("Reading file contents:\n");
    while (fgets(buffer, sizeof(buffer), fp)) {
        printf("%s", buffer);
    }

    fclose(fp);
}

void appendToFile() {
    FILE *fp = fopen("demo.txt", "a");
    if (fp == NULL) {
        printf("Error opening file for appending!\n");
        return;
    }

    fprintf(fp, "Appending this line.\n");
    fclose(fp);
    printf("Appended to file.\n");
}

int main() {
    writeToFile();

    printf("\n--- Reading after write ---\n");
    readFromFile();

    printf("\n--- Appending to file ---\n");
    appendToFile();

    printf("\n--- Reading after append ---\n");
    readFromFile();

    return 0;
}

