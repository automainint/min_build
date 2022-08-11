#include "../mul/mul.h"

#include <stdio.h>
#include <stdlib.h>

int sum_mul(int const size, int const *numbers) {
  int s = 0;

  for (int i = 1; i < size; i += 2)
    s += mul(numbers[i - 1], numbers[i]);

  return s;
}

enum {
  MAX_SIZE = 400
};

int main(int argc, char **argv) {
  int const size = argc - 1;
  int numbers[MAX_SIZE];

  if (size > MAX_SIZE) {
    printf("Too many arguments!\n");
    return 1;
  }

  for (int i = 0; i < size; i++)
    numbers[i] = atoi(argv[1 + i]);

  printf("%d\n", sum_mul(size, numbers));

  return 0;
}
