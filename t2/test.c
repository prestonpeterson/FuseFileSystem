#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int main(void) {
	srand(time(NULL));
	char a = 65 + (rand() % 58);
	printf("%c\n", a);
	int b = 'A';
	printf("%d\n", b);


  static char *random_str = "";
  srand(time(NULL));
  size_t len = rand() % 256;
  random_str = malloc(len * sizeof(char));
  char random_char;
  int i;
  for (i = 0; i < len-1; i++) {
    random_char = 65 + (rand() % 58);
    random_str[i] = random_char;
  }
  printf("%s\n", random_str);


	return 0;
}