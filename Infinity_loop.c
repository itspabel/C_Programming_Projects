#include <stdio.h>

int main() {
  char word[100]; // Array to store the word (max 100 characters)

  printf("Enter a word: ");
  scanf("%s", word); // Read the word from the user

  // This loop will continue indefinitely
  while (1) {
    printf("%s\n", word); // Print the word
  }

  return 0;
}