        .include "x16.inc"
        .import _mixer_set_period, mx_ch, mx_period
        .importzp s0f
        .export do_set_period
        .code
do_set_period:
        jmp _mixer_set_period
