.global calc_wave

calc_wave:
	mov r3, #4
	mul r2, r1, r3
	cmp r0, r2
	bge .end		
	mov r0, r1
	mov pc, lr
.end:
	mov r0, r1
	add r0, r0, #1
	mov pc, lr
