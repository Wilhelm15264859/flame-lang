	.file	"flame"
	.text
	.globl	main                            # -- Begin function main
	.p2align	4
	.type	main,@function
main:                                   # @main
	.cfi_startproc
# %bb.0:                                # %entry
	pushq	%rbx
	.cfi_def_cfa_offset 16
	subq	$16, %rsp
	.cfi_def_cfa_offset 32
	.cfi_offset %rbx, -16
	movq	$.Lstr, 8(%rsp)
	#APP

	movq	$1, %rax

	#NO_APP
	#APP

	movq	$1, %rdi

	#NO_APP
	movq	8(%rsp), %r8
	#APP

	movq	%r8, %rsi

	#NO_APP
	#APP

	movq	$6, %rdx

	#NO_APP
	#APP

	syscall

	#NO_APP
	xorl	%eax, %eax
	addq	$16, %rsp
	.cfi_def_cfa_offset 16
	popq	%rbx
	.cfi_def_cfa_offset 8
	retq
.Lfunc_end0:
	.size	main, .Lfunc_end0-main
	.cfi_endproc
                                        # -- End function
	.type	.Lstr,@object                   # @str
	.section	.rodata.str1.1,"aMS",@progbits,1
.Lstr:
	.asciz	"Hello\n"
	.size	.Lstr, 7

	.section	".note.GNU-stack","",@progbits
