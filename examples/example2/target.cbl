; Check if rdx (memcpy size) indicates underflow at the EXTRA state memcpy call.
; At 0x16ebdf: memcpy(dst, src, rdx) where rdx = extra_max - len.
; When len > extra_max, the sub underflows (unsigned), giving a huge rdx.
(declare @reached ((x Bool)) Unit)

(defun @target ((regs X86Regs)) Unit
  (start start:
    (let rdx_ (get-reg rdx regs))
    (let rdx (pointer-to-bits rdx_))
    (let threshold (bv 64 0x7FFFFFFF))
    (let overflow (< threshold rdx))
    (funcall @reached overflow)
    (return ())))
