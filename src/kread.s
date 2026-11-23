; kread.s -- file reading through the KERNAL, for the MOD loader.
;
; cc65's cbm_read() returns 0 on this setup even though the file opens
; cleanly, while BASIC's OPEN/GET# on the same file works. Rather than fight
; the library, this does what BASIC does: SETNAM/SETLFS/OPEN, then CHKIN and
; BASIN per byte, then CLRCHN.
;
; Bulk transfers go through MACPTR ($FF44), which moves up to 255 bytes per
; call instead of one. Per-byte BASIN costs 100-200 cycles a byte -- an 11
; second load for a 350 KB module, which makes a playlist unusable. MACPTR
; returns carry set on a device that does not support it, so the BASIN loop
; stays as the fallback.
;
; Arguments go in globals rather than cc65's parameter stack.

SETNAM = $FFBD
SETLFS = $FFBA
OPEN   = $FFC0
CLOSE  = $FFC3
CHKIN  = $FFC6
CLRCHN = $FFCC
BASIN  = $FFCF
READST = $FFB7
MACPTR = $FF44

LFN    = 2

        .export _kfile_open, _kfile_read, _kfile_close
        .export _kf_name, _kf_namelen, _kf_dest, _kf_count, _kf_read, _kf_eof
        .export _kf_slow, _kf_sa

        .zeropage
kf_dst: .res 2

        .bss
_kf_name:    .res 2             ; pointer to the filename
_kf_namelen: .res 1
_kf_dest:    .res 2             ; destination address
_kf_count:   .res 2             ; bytes requested
_kf_read:    .res 2             ; bytes actually read
_kf_eof:     .res 1             ; non-zero once the file has ended
_kf_slow:    .res 1             ; force the byte-at-a-time path
_kf_sa:      .res 1             ; secondary address for the next open
kf_got:      .res 2             ; bytes moved by the last MACPTR

        .code

; ---------------------------------------------------------------------------
; _kfile_open -- _kf_name/_kf_namelen, secondary address in _kf_sa.
; Returns 0 in A on success.
;
; The secondary address is NOT fixed. A data file is opened on a general
; purpose channel (2), but the directory "$" is served on the LOAD channel
; (0) -- BASIC's own OPEN 1,8,0,"$" is the proof. Opening "$" on channel 2
; is tolerated by a host filesystem and hangs a real CMDR-DOS device.
; ---------------------------------------------------------------------------
_kfile_open:
        stz _kf_eof
        lda _kf_namelen
        ldx _kf_name
        ldy _kf_name+1
        jsr SETNAM
        lda #LFN
        ldx #8                          ; device 8
        ldy _kf_sa                      ; secondary address
        jsr SETLFS
        jsr OPEN
        bcs ko_err
        lda #0
        rts
ko_err: rts                             ; KERNAL error code already in A

; ---------------------------------------------------------------------------
; _kfile_read -- read _kf_count bytes to _kf_dest; _kf_read gets the count.
; Stops early at end of file and sets _kf_eof.
; ---------------------------------------------------------------------------
_kfile_read:
        stz _kf_read
        stz _kf_read+1
        lda _kf_dest
        sta kf_dst
        lda _kf_dest+1
        sta kf_dst+1

        ldx #LFN
        jsr CHKIN
        bcs kr_giveup

        ; The directory channel asks for the plain path. MACPTR is proven for
        ; reading module files, but "$" is served by a different part of the
        ; DOS and a listing is only a few kilobytes, so there is nothing to win
        ; there and a whole class of device-specific behaviour to avoid.
        lda _kf_slow
        beq kr_bulk
        jmp kr_slow
kr_giveup:
        jmp kr_done

; --- bulk path: up to 255 bytes per MACPTR call ---------------------------
kr_bulk:
        lda _kf_count+1                 ; how much to ask for this time
        bne kr_max
        lda _kf_count
        bne kr_part
        jmp kr_done                     ; nothing left
kr_part:
        cmp #255
        bcc kr_ask
kr_max: lda #255
kr_ask:
        ldx kf_dst
        ldy kf_dst+1
        clc                             ; clear = advance the destination
        jsr MACPTR
        bcs kr_slow                     ; device has no MACPTR -> byte loop

        ; X/Y = bytes actually transferred, possibly short.
        stx kf_got
        sty kf_got+1

        ; The KERNAL documentation says this is always greater than zero. Do
        ; not rely on it: a device that returns zero with carry clear and no
        ; EOF status would leave the byte count unchanged and spin here
        ; forever, hammering the drive -- a hang with the activity light
        ; stuck on. Treat a zero-length transfer as end of file.
        txa
        ora kf_got+1
        bne kr_got
        jmp kr_done
kr_got:

        clc                             ; dest += got
        lda kf_dst
        adc kf_got
        sta kf_dst
        lda kf_dst+1
        adc kf_got+1
        sta kf_dst+1

        clc                             ; read += got
        lda _kf_read
        adc kf_got
        sta _kf_read
        lda _kf_read+1
        adc kf_got+1
        sta _kf_read+1

        sec                             ; count -= got
        lda _kf_count
        sbc kf_got
        sta _kf_count
        lda _kf_count+1
        sbc kf_got+1
        sta _kf_count+1

        jsr READST
        beq kr_bulk
        sta _kf_eof
        jmp kr_done

; --- fallback: one byte at a time -----------------------------------------
kr_slow:
        lda _kf_count
        ora _kf_count+1
        beq kr_done

        jsr BASIN
        ldy #0
        sta (kf_dst),y

        inc kf_dst
        bne kr_n1
        inc kf_dst+1
kr_n1:  inc _kf_read
        bne kr_n2
        inc _kf_read+1
kr_n2:  lda _kf_count                   ; 16-bit decrement
        bne kr_n3
        dec _kf_count+1
kr_n3:  dec _kf_count

        jsr READST
        beq kr_slow
        sta _kf_eof

kr_done:
        jsr CLRCHN
        lda _kf_dest                    ; leave _kf_dest past what was read
        clc
        adc _kf_read
        sta _kf_dest
        lda _kf_dest+1
        adc _kf_read+1
        sta _kf_dest+1
        rts

_kfile_close:
        lda #LFN
        jsr CLOSE
        jmp CLRCHN

; ---------------------------------------------------------------------------
; _kfile_cmd -- send a DOS command on the command channel.
;
; Opening logical file 15 with secondary address 15 hands the filename to the
; drive as a command, so "CD:NAME" is issued simply by opening and closing it.
; Uses _kf_name / _kf_namelen like the other entry points. Returns 0 in A.
; ---------------------------------------------------------------------------
        .export _kfile_cmd
_kfile_cmd:
        lda _kf_namelen
        ldx _kf_name
        ldy _kf_name+1
        jsr SETNAM
        lda #15                         ; logical file
        ldx #8                          ; device
        ldy #15                         ; secondary address = command channel
        jsr SETLFS
        jsr OPEN
        lda #15
        jsr CLOSE
        jsr CLRCHN
        lda #0
        rts

; ---------------------------------------------------------------------------
; _ram_banks -- number of 8 KB RAM banks the machine actually has.
;
; MEMTOP with carry set reports it. A stock X16 has 512 KB (64 banks); an
; expanded one has 2 MB (256, reported as 0). Large modules do not fit in the
; stock machine, and the loader has to say so up front instead of reading half
; a megabyte before finding out.
; ---------------------------------------------------------------------------
MEMTOP = $FF99

        .export _ram_banks
_ram_banks:
        sec
        jsr MEMTOP                      ; -> .A = bank count (0 means 256)
        ldx #0
        cmp #0
        bne rb_done
        lda #255                        ; clamp: we only ever compare against it
rb_done:
        rts
