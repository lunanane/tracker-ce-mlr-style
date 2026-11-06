@ Sequencer tracks mod (plan 019 step 0) - PolyForm Noncommercial 1.0.0, see seqgen/LICENSE.md. Own code, written for this mod.
@
@ Word-wise memcpy and memset. newlib-nano (-specs=nano.specs) links plain byte loops (ldrb / strb, about 6 cycles per byte),
@ and the Teensy memcpy-armv7m.S / memset.S in 3rdparty/teensy_core are empty here (assembled without -mcpu, so their
@ #if is false). The audio interrupt copies and clears 256 byte blocks all the time (AudioStream copy-on-write, mono to
@ stereo, bypass copies, muted gain clears, envelope clears). Same contract as libc: no overlap handling, returns the
@ destination. Both pointers 4-byte aligned: 16 bytes per loop (ldm / stm), then words, then bytes. Any other alignment:
@ the byte loop (no unaligned word access: it would trap with UNALIGN_TRP and gains nothing on the SDRAM bus).
@ Defining the symbols here makes the linker skip nano's memcpy.o / memset.o. memmove, memcmp, strlen stay nano's.
	.cpu	cortex-m4
	.syntax unified
	.thumb
	.text

	.global	memcpy
	.type	memcpy, %function
	.thumb_func
	.p2align 2
memcpy:
	orr	r3, r0, r1
	push	{r0, r4, r5}		@ r0: the return value
	lsls	r3, r3, #30		@ Z set when both pointers are word aligned
	bne	.Lcp_bytes
	subs	r2, r2, #16
	blo	.Lcp_w16_end
.Lcp_w16:
	ldmia	r1!, {r3, r4, r5, r12}
	stmia	r0!, {r3, r4, r5, r12}
	subs	r2, r2, #16
	bhs	.Lcp_w16
.Lcp_w16_end:
	adds	r2, r2, #16
.Lcp_w4:
	subs	r2, r2, #4
	blo	.Lcp_w4_end
	ldr	r3, [r1], #4
	str	r3, [r0], #4
	b	.Lcp_w4
.Lcp_w4_end:
	adds	r2, r2, #4
.Lcp_bytes:
	cbz	r2, .Lcp_done
.Lcp_b:
	ldrb	r3, [r1], #1
	strb	r3, [r0], #1
	subs	r2, r2, #1
	bne	.Lcp_b
.Lcp_done:
	pop	{r0, r4, r5}
	bx	lr
	.size	memcpy, . - memcpy

	.global	memset
	.type	memset, %function
	.thumb_func
	.p2align 2
memset:
	push	{r4, r5}
	mov	r12, r0			@ r0 stays the return value
	uxtb	r1, r1
	orr	r1, r1, r1, lsl #8
	orr	r1, r1, r1, lsl #16	@ the fill byte in all four lanes
.Lms_head:
	tst	r12, #3
	beq	.Lms_aligned
	cbz	r2, .Lms_done
	strb	r1, [r12], #1
	subs	r2, r2, #1
	b	.Lms_head
.Lms_aligned:
	mov	r3, r1
	mov	r4, r1
	mov	r5, r1
	subs	r2, r2, #16
	blo	.Lms_w16_end
.Lms_w16:
	stmia	r12!, {r1, r3, r4, r5}
	subs	r2, r2, #16
	bhs	.Lms_w16
.Lms_w16_end:
	adds	r2, r2, #16
.Lms_w4:
	subs	r2, r2, #4
	blo	.Lms_w4_end
	str	r1, [r12], #4
	b	.Lms_w4
.Lms_w4_end:
	adds	r2, r2, #4
.Lms_b:
	cbz	r2, .Lms_done
	strb	r1, [r12], #1
	subs	r2, r2, #1
	b	.Lms_b
.Lms_done:
	pop	{r4, r5}
	bx	lr
	.size	memset, . - memset
