; KTOS MSP430G2553 task launch trampoline
;
; KTOS — Tiny Cooperative Task Switcher
; Copyright (C) 2004-2025 Khalid Hamdou / BAAMIIS LIMITED
; SPDX-License-Identifier: GPL-3.0-only
;
; On entry (restored from saved context):
;   R4 = initial_msg_type  (→ R12 first arg)
;   R5 = initial_sparam    (→ R13 second arg)
;   R6 = lParam low        (→ R14)
;   R7 = lParam high       (→ R15)
;   R8 = task_func_addr
; Stack: [SP] = task_exit_handler_addr

    .text
    .global ktos_task_launch
ktos_task_launch:
    mov  r4, r12
    mov  r5, r13
    mov  r6, r14
    mov  r7, r15
    br   r8          ; jump (not call) to task function
                     ; when task returns, pops exit_handler from stack
