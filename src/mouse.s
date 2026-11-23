; mouse.s -- KERNAL mouse wrappers for the player GUI.
;
; The pointer itself is a VERA hardware sprite that the KERNAL draws and
; moves, so it costs the player nothing per frame -- no save/restore of the
; area under the cursor, no redraw when it moves over the pattern view.
;
; mouse_get wants the offset of a ZERO PAGE location in .X, which it fills
; with X and Y as two 16-bit values, and returns the button mask in .A.

screen_mode  = $FF5F
mouse_config = $FF68
mouse_get    = $FF6B

        .export _mouse_init, _mouse_poll
        .export _mouse_x, _mouse_y, _mouse_buttons

        .zeropage
mpos:   .res 4                          ; x lo/hi, y lo/hi -- must be in ZP

        .bss
_mouse_x:       .res 2
_mouse_y:       .res 2
_mouse_buttons: .res 1

        .code

; ---------------------------------------------------------------------------
; _mouse_init -- show the default pointer, sized to the current screen mode.
; ---------------------------------------------------------------------------
_mouse_init:
        sec                             ; carry set = query the current mode
        jsr screen_mode                 ; -> .X, .Y = size in 8px cells
        lda #1                          ; 1 = default arrow, visible
        jsr mouse_config
        stz _mouse_buttons
        rts

; ---------------------------------------------------------------------------
; _mouse_poll -- refresh _mouse_x / _mouse_y / _mouse_buttons.
;
; Deliberately does NOT call mouse_scan. The default KERNAL interrupt handler
; already scans the mouse once a frame, and the reference is explicit that the
; routine "should only be called if the interrupt handler has been completely
; replaced" -- which this player does not do. Calling it here as well applied
; the pointer's movement more than once per frame: the poll runs from pump(),
; which is called from ten places in the render and draw paths and spins far
; faster than 60 Hz, so the pointer flew across the screen and jumped.
;
; mouse_get only reads the state the IRQ already saved, so polling it as often
; as we like is free and simply returns the same position until the next frame.
; ---------------------------------------------------------------------------
_mouse_poll:
        ldx #mpos                       ; zero-page offset, not an address
        jsr mouse_get
        sta _mouse_buttons
        lda mpos+0
        sta _mouse_x
        lda mpos+1
        sta _mouse_x+1
        lda mpos+2
        sta _mouse_y
        lda mpos+3
        sta _mouse_y+1
        rts
