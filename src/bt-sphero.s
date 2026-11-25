    .arch armv8-a
    .text
    .p2align 2

    .global build_rgb_packet
    .type build_rgb_packet, %function

// unsigned build_rgb_packet(
//     uint8_t* out,       // x0
//     unsigned red,       // w1
//     unsigned green,     // w2
//     unsigned blue,      // w3
//     unsigned sequence   // w4
// );
//
// Writes 11 bytes and returns 11 in w0.
// Leaf function: uses only caller-saved registers.

build_rgb_packet:
    // Packet start: FF FE.
    mov     w5, #0xff
    strb    w5, [x0, #0]

    mov     w5, #0xfe
    strb    w5, [x0, #1]

    // DID = 02: Sphero device commands.
    mov     w5, #0x02
    strb    w5, [x0, #2]

    // CID = 20: set RGB LED.
    mov     w5, #0x20
    strb    w5, [x0, #3]

    // Sequence number.
    strb    w4, [x0, #4]

    // DLEN = 4 payload bytes + 1 checksum byte.
    mov     w5, #5
    strb    w5, [x0, #5]

    // Payload: red, green, blue, save flag.
    strb    w1, [x0, #6]
    strb    w2, [x0, #7]
    strb    w3, [x0, #8]
    strb    wzr, [x0, #9]

    // Sum bytes 2 through 9.
    mov     w5, #0
    mov     x6, #2

.Lchecksum:
    ldrb    w7, [x0, x6]
    add     w5, w5, w7
    add     x6, x6, #1
    cmp     x6, #10
    b.lo    .Lchecksum

    // Invert sum; STRB stores only its low 8 bits.
    mvn     w5, w5
    strb    w5, [x0, #10]

    mov     w0, #11
    ret

    .size build_rgb_packet, .-build_rgb_packet

    .section .note.GNU-stack,"",%progbits
