/*
 * Copyright (c) Dmitry Osipenko 2018
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

pseq_to_dw_exec_nb = 4	// the number of 'EXEC' block where DW happens
alu_buffer_size = 1	// number of .rgba regs carried through pipeline

.asm

EXEC
	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t1.fp20, t1.fp20, NOP, NOP

	// sample tex1 (mask)
	TEX:	tex r2, r3, tex0, r0, r1, r2
;

EXEC
	MFU:	sfu:  rcp r4
		mul0: bar, sfu, bar0
		mul1: bar, sfu, bar1
		ipl:  t0.fx10, t0.fx10, NOP, NOP

	ALU:
		ALU0:	MAD  r0.l, r2.l,  r0.l, #0
		ALU1:	MAD  r0.h, r2.h,  r0.h, #0
		ALU2:	MAD  r1.l, r3.l,  r1.l, #0
		ALU3:	MAD  r1.h, r3.h, -r1.h, #1
;

EXEC
	// fetch dst pixel to r2,r3
	PSEQ:	0x0081000A

	ALU:
		ALU0:	MAD  r0.l, r1.h, r2.l, r0.l
		ALU1:	MAD  r0.h, r1.h, r2.h, r0.h
		ALU2:	MAD  r1.l, r1.h, r3.l, r1.l
;

EXEC
	ALU:
		ALU0:	MAD  lp.lh, r0.l, #1, -r2.l
		ALU1:	MAD  lp.lh, r0.h, #1, -r2.h
		ALU2:	MAD  lp.lh, r1.l, #1, -r3.l
		ALU3:	MAD  lp.lh, r1.h, #0, -r3.h

	ALU:
		ALU0:	MAD  lp.lh, abs(alu0), #1, #0 (this)
		ALU1:	MAD  lp.lh, abs(alu1), #1, #0 (other)
		ALU2:	MAD  lp.lh, abs(alu2), #1, #0 (other)
		ALU3:	MAD  lp.lh, abs(alu3), #1, #0

	// kill the pixel if dst is unchanged
	ALU:
		ALU0:	CSEL kill, -alu0, #0, #1

	DW:	store rt1, r0, r1
;
