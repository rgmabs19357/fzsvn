/* cmpxchg8btest.s: test CMPXCHG8B instruct */

	.section .data
value1:
	.byte 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88

value2:
	.byte 0x12, 0x21, 0x23, 0x32, 0x34, 0x43, 0x45, 0x54

	.section .text
	.global _start

_start:
	nop
	movl $0x44332211, %eax
	movl $0x88776655, %edx
	movl $0x12345678, %ecx
	movl $0x87654321, %ebx
	cmpxchg8b value1

	movl $0x1234, %edx
	movl $0x0, %eax
	movl $0x1, %ecx
	movl $0x2, %ebx
	cmpxchg8b value2

_exit:
	movl $0, %ebx
	movl $1, %eax
	int $0x80

	