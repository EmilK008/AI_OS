/* Minimal setjmp/longjmp for i386 (MinGW PE: C names get leading underscore in asm). */
#ifndef AI_OS_SETJMP_H
#define AI_OS_SETJMP_H

typedef struct {
    unsigned long ebx;
    unsigned long esi;
    unsigned long edi;
    unsigned long ebp;
    unsigned long esp;
    unsigned long eip;
} jmp_buf[1];

int setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));

#endif
