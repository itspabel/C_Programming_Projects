#include <stdio.h>

int main() {
    char word[100]; // Array to store the input word

    printf("Enter a word: ");
    scanf("%99s", word); // Read the input word

    for (int i = 0; i < 50; i++) {
        printf("%s ", word); // Print the word
    }

    return 0;
}
//Code by Pabel