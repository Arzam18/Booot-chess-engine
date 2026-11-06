.section .rodata.booot_nnue, "a", %progbits
.balign 64
.global booot2_nn_start
.type booot2_nn_start, %object
booot2_nn_start:
.incbin "../booot2.nn"
.global booot2_nn_end
booot2_nn_end:
.size booot2_nn_start, booot2_nn_end - booot2_nn_start

.balign 64
.global booot4_nn_start
.type booot4_nn_start, %object
booot4_nn_start:
.incbin "../booot4.nn"
.global booot4_nn_end
booot4_nn_end:
.size booot4_nn_start, booot4_nn_end - booot4_nn_start

.balign 64
.global booot58_nn_start
.type booot58_nn_start, %object
booot58_nn_start:
.incbin "../booot58.nn"
.global booot58_nn_end
booot58_nn_end:
.size booot58_nn_start, booot58_nn_end - booot58_nn_start

.section .note.GNU-stack, "", %progbits
