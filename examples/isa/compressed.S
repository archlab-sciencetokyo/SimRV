.option rvc

.section .text.init
.global _start
.global checkpoint_compressed
.global done

_start:
    c.li    a0, 1
    c.addi  a0, 2
    c.slli  a0, 1
    c.mv    a1, a0
    c.add   a1, a0

checkpoint_compressed:
    # Expected values: a0 = 6, a1 = 12. Inspect the 16-bit encodings in disassembly.
    c.nop
done:
    c.j     done
