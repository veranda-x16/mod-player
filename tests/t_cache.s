; t_cache.s -- drive cache_refill for voice 0 from state poked by the driver.
        .include "x16.inc"
        .include "cycles.inc"
        .import cache_refill, cache_init, cache_consume
        .import cache0, cache1, cache2, cache3
        .import c_bank, c_addr, c_ebank, c_eaddr, c_lbank, c_laddr
        .import c_loop, c_active
        .export do_refill, do_init, bench_refill, cyc_result, bench_null
        .export do_consume, consume_n, bench_consume

        .bss
cyc_result: .res 4
consume_n:  .res 1

        .code
do_init:
        jmp cache_init
do_refill:
        ldx #0
        jmp cache_refill
do_consume:
        ldx #0
        lda consume_n
        jmp cache_consume
bench_consume:
        ldx #0
        lda consume_n
        CYC_START
        jsr cache_consume
        CYC_STOP cyc_result
        rts
bench_null:
        CYC_START
        CYC_STOP cyc_result
        rts
bench_refill:
        ldx #0
        CYC_START
        jsr cache_refill
        CYC_STOP cyc_result
        rts
