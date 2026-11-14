/*
 * SPDX-FileCopyrightText: 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#if defined(__ARM_FEATURE_BTI_DEFAULT) && __ARM_FEATURE_BTI_DEFAULT
#define CAMCOLOR_BTI_C bti c
#define CAMCOLOR_FEATURE_BTI 1
#else
#define CAMCOLOR_BTI_C
#define CAMCOLOR_FEATURE_BTI 0
#endif

#if defined(__ARM_FEATURE_PAC_DEFAULT) && __ARM_FEATURE_PAC_DEFAULT
#define CAMCOLOR_SIGN paciasp
#define CAMCOLOR_AUTH autiasp
#define CAMCOLOR_FEATURE_PAC 2
#else
#define CAMCOLOR_SIGN
#define CAMCOLOR_AUTH
#define CAMCOLOR_FEATURE_PAC 0
#endif

	.hidden camcolor_slots
	.hidden camcolor_resolve

	.macro camcolor_trampoline index, name, bind
	.text
	.p2align 4
	.\bind \name
	.type \name, %function
\name:
	CAMCOLOR_BTI_C
	adrp x16, camcolor_slots + 8 * \index
	add x16, x16, :lo12:camcolor_slots + 8 * \index
	ldar x16, [x16]
	cbz x16, 1f
	br x16
1:
	mov x17, #\index
	b camcolor_slow
	.size \name, . - \name
	.endm

#define CAMCOLOR_FORWARD(index, name, bind) camcolor_trampoline index, name, bind
#define CAMCOLOR_WRAPPED(index, name)
#define CAMCOLOR_DATA(name)
#include "exports.def"

	.text
	.p2align 4
	.type camcolor_slow, %function
camcolor_slow:
	CAMCOLOR_SIGN
	stp x29, x30, [sp, #-224]!
	mov x29, sp
	stp x0, x1, [sp, #16]
	stp x2, x3, [sp, #32]
	stp x4, x5, [sp, #48]
	stp x6, x7, [sp, #64]
	str x8, [sp, #80]
	stp q0, q1, [sp, #96]
	stp q2, q3, [sp, #128]
	stp q4, q5, [sp, #160]
	stp q6, q7, [sp, #192]
	mov x0, x17
	bl camcolor_resolve
	mov x16, x0
	ldp q6, q7, [sp, #192]
	ldp q4, q5, [sp, #160]
	ldp q2, q3, [sp, #128]
	ldp q0, q1, [sp, #96]
	ldr x8, [sp, #80]
	ldp x6, x7, [sp, #64]
	ldp x4, x5, [sp, #48]
	ldp x2, x3, [sp, #32]
	ldp x0, x1, [sp, #16]
	ldp x29, x30, [sp], #224
	CAMCOLOR_AUTH
	br x16
	.size camcolor_slow, . - camcolor_slow

#if CAMCOLOR_FEATURE_BTI || CAMCOLOR_FEATURE_PAC
	.pushsection .note.gnu.property, "a"
	.p2align 3
	.word 4
	.word 16
	.word 5
	.asciz "GNU"
	.word 0xc0000000
	.word 4
	.word (CAMCOLOR_FEATURE_BTI | CAMCOLOR_FEATURE_PAC)
	.word 0
	.popsection
#endif

	.section .note.GNU-stack, "", %progbits
