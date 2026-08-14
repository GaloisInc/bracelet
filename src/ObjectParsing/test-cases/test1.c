#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int foo = 12;
int ethan = 749;
int baz = 42;

struct Value {
  unsigned long number1;
  void *ptr;
  unsigned long number2;
};
struct Value __attribute__((section("test_section"))) value = {123, &foo, 456};

void *__attribute__((section("function_pointers"))) my_function_pointers[] = {
    perror, abort, exit};

int *__attribute__((section("int_pointers"))) my_int_pointers[] = {&ethan,
                                                                   &baz};

int __attribute__((section("my_arr_section"))) my_arr[256] = {1, 2, 3};
int *__attribute__((section("my_arr_pointers_section"))) my_pointers[] = {
    &my_arr[1], &my_arr[4], &my_arr[75]};

int main() {
  char cmd_buffer[1024];
  snprintf(cmd_buffer, sizeof(cmd_buffer), "gcore %d", (int)getpid());
  return system(cmd_buffer);
}
