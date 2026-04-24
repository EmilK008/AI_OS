/* i386 cdecl setjmp/longjmp for AI_OS (PE / MinGW underscore symbols). */
.text
.globl _setjmp
_setjmp:
    movl    4(%esp), %edx
    movl    (%esp), %eax
    movl    %ebx, 0(%edx)
    movl    %esi, 4(%edx)
    movl    %edi, 8(%edx)
    movl    %ebp, 12(%edx)
    movl    %esp, 16(%edx)
    movl    %eax, 20(%edx)
    xorl    %eax, %eax
    ret

.globl _longjmp
_longjmp:
    movl    4(%esp), %edx
    movl    8(%esp), %eax
    testl   %eax, %eax
    jnz     1f
    movl    $1, %eax
1:
    movl    0(%edx), %ebx
    movl    4(%edx), %esi
    movl    8(%edx), %edi
    movl    12(%edx), %ebp
    movl    16(%edx), %esp
    /* Saved esp points at setjmp's return address; ret pops it and resumes
     * in puff() with eax = longjmp value (must not jmp to eip with wrong esp). */
    ret
