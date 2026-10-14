#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

static uint64_t allocation[128];
static unsigned int allocated;

static void stop(unsigned int status) {
    register unsigned int r0 __asm__("$r0") = status;
    __asm__ volatile("syscall 1" : : "r"(r0) : "memory");
    __builtin_unreachable();
}

void* malloc(size_t size) {
    if (allocated || size > sizeof(allocation))
        stop(2);
    allocated = 1;
    return allocation;
}

void free(void* pointer) {
    if (!allocated || pointer != allocation)
        stop(2);
    allocated = 0;
}

void* memcpy(void* destination, const void* source, size_t size) {
    uint8_t* output = destination;
    const uint8_t* input = source;
    for (size_t i = 0; i < size; ++i)
        output[i] = input[i];
    return destination;
}

void* memset(void* destination, int value, size_t size) {
    uint8_t* output = destination;
    for (size_t i = 0; i < size; ++i)
        output[i] = (uint8_t)value;
    return destination;
}

void* memmove(void* destination, const void* source, size_t size) {
    uint8_t* output = destination;
    const uint8_t* input = source;
    if ((uintptr_t)output <= (uintptr_t)input)
        return memcpy(output, input, size);
    while (size--)
        output[size] = input[size];
    return destination;
}

int printf(const char* format, ...) {
    if (format[0] != '%' || format[1] != 'u')
        return 0;
    va_list arguments;
    va_start(arguments, format);
    uint32_t result[8] = {0};
    result[0] = va_arg(arguments, unsigned int);
    result[1] = va_arg(arguments, unsigned int);
    va_end(arguments);
    register unsigned int r0 __asm__("$r0") = 1;
    register void* r1 __asm__("$r1") = result;
    register unsigned int r2 __asm__("$r2") = sizeof(result);
    __asm__ volatile("syscall 5" : "+r"(r0) : "r"(r1), "r"(r2) : "memory");
    return 0;
}
