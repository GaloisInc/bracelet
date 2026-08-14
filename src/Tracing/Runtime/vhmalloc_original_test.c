// From
// https://gitlab-ext.galois.com/radss/vhmalloc/-/blob/d63edbb16de64d7a9bcb7dfb02b566b12b150e58/test/runtests.c

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/time.h>

void filldata(intptr_t *memory, size_t size) {
  intptr_t fill = ((intptr_t)memory) + 1;
  for (size_t i = 0; i < (size / sizeof(intptr_t)); i++) {
    memory[i] = fill;
  }
  memcpy(&memory[size / sizeof(intptr_t)], &fill, size % sizeof(intptr_t));
}

void checkdata(intptr_t *memory, size_t size) {
  intptr_t fill = ((intptr_t)memory) + 1;
  for (size_t i = 0; i < (size / sizeof(intptr_t)); i++) {
    if (memory[i] != fill) {
      fprintf(stderr, "Corruption detected in object %p at address %p\n",
              memory, &memory[i]);
      exit(-1);
    }
  }
  if (0 != memcmp(&memory[size / sizeof(intptr_t)], &fill,
                  size % sizeof(intptr_t))) {
    fprintf(stderr, "Corruption detected in object %p at address %p\n", memory,
            &memory[size / sizeof(intptr_t)]);
    exit(-1);
  }
  filldata(memory, size);
}

int main(int argc, char **argv) {
  int iters = 1000;
  int num = 1000;
  int seed = 42;
  int input = 0;
  int output = 0;
  int diag = 0;
  int opt;
  while ((opt = getopt(argc, argv, "iodc:n:s:")) != -1) {
    switch (opt) {
    case 'i':
      input = 1;
      break;
    case 'o':
      output = 1;
      break;
    case 'd':
      diag = 1;
      break;
    case 'c':
      iters = atoi(optarg);
      break;
    case 'n':
      num = atoi(optarg);
      break;
    case 's':
      seed = atoi(optarg);
      break;
    }
  }

  srand(seed);

  size_t allocated = 0;
  size_t allocatedSize = 0;
  struct rusage usage;
  getrusage(RUSAGE_SELF, &usage);
  size_t initialHeapSize = usage.ru_maxrss;

  if (input) {
    int ret = scanf("max: %d\n", &num);
    if (ret != 1) {
      exit(0);
    }

    if (num <= 0 || num > 4096) {
      exit(0);
    }

    intptr_t **pointers = malloc(sizeof(intptr_t) * num);
    if (pointers == NULL) {
      exit(0);
    }
    size_t *size = malloc(sizeof(intptr_t) * num);
    if (size == NULL) {
      exit(0);
    }

    int cont = 1;
    while (cont) {
      int index, newsize;
      if (2 == scanf("M %d %d\n", &index, &newsize)) {
        if (index < 0 || index >= num) {
          exit(0);
        }
        if (newsize < 0 || newsize >= 10000) {
          exit(0);
        }
        pointers[index] = malloc(newsize);
        size[index] = newsize;
        filldata(pointers[index], newsize);
      } else if (1 == scanf("F %d\n", &index)) {
        if (index < 0 || index >= num) {
          exit(0);
        }
        if (size[index] != 0) {
          checkdata(pointers[index], size[index]);
          free(pointers[index]);
          size[index] = 0;
        }
      } else if (1 == scanf("C %d\n", &index)) {
        if (index < 0 || index >= num) {
          exit(0);
        }
        if (size[index] != 0) {
          checkdata(pointers[index], size[index]);
        }
      } else {
        cont = 0;
      }
    }
  } else {
    fprintf(stdout, "max: %d\n", num);
    intptr_t **pointers = malloc(sizeof(intptr_t) * num);
    size_t *size = malloc(sizeof(intptr_t) * num);

    for (int i = 1; i <= iters; i++) {
      int index = rand() % num;
      if (size[index] == 0) {
        int newsize = (1 << (rand() % 12));
        allocated++;
        allocatedSize += newsize;
        if (output) {
          fprintf(stdout, "M %d %d\n", index, newsize);
        }
        pointers[index] = malloc(newsize);
        size[index] = newsize;
        filldata(pointers[index], newsize);
      } else {
        if (output) {
          fprintf(stdout, "C %d\n", index);
        }
        checkdata(pointers[index], size[index]);
        if (rand() % 2) {
          if (output) {
            fprintf(stdout, "F %d\n", index);
          }
          allocated--;
          allocatedSize -= size[index];
          free(pointers[index]);
          size[index] = 0;
        }
      }
      if (diag) {
        if (i % 10000 == 0) {
          printf("Finished iteration %d\n", i);
          for (int index = 0; index < num; index++) {
            if (size[index] != 0) {
              checkdata(pointers[index], size[index]);
            }
          }
          printf("%ld objects allocated with total size %ld\n", allocated,
                 allocatedSize);
          getrusage(RUSAGE_SELF, &usage);
          size_t heapsize = (usage.ru_maxrss - initialHeapSize) * 1024;
          printf("memory size: %ld usage: %.2f%%\n", heapsize,
                 100 * ((float)allocatedSize / (float)heapsize));
        }
      }
    }
  }
}
