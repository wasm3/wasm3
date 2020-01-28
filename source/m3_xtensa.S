
    .global     runOp
    .type       runOp,@function
    .align      4

/* m3ret_t runOp(pc_t _pc, u64 * _sp, m3reg_t _r0) */
runOp:

    entry sp, 16
    /* Incoming arguments: a2: _pc, a3: _sp, a4-a5: _r0 */

    /* Outgoing arguments (for call8): a6: _pc, a7: _sp, a8-a9: _r0 */
    mov a9, a5
    mov a8, a4
    mov a7, a3
    mov a6, a2

1:
    l32i a2, a6, 0
    addi a6, a6, 4
    callx4 a2
    bnez a7, 1b

    /* Set return value */
    or a2, a6, a6
    retw
