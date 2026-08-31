; Override for strcmp(s1, s2) -> int
; Byte-by-byte comparison that works with solver-constrained memory
; (the built-in strcmp requires concretely evaluable bytes, which fails
; when .rodata is loaded via solver assumptions rather than concrete writes).
; Returns 0 if equal, non-zero otherwise.

(defun @strcmp ((s1 (Ptr 64)) (s2 (Ptr 64))) (Ptr 64)
  (registers ($p1 (Ptr 64)) ($p2 (Ptr 64)))
  (start start:
    (set-register! $p1 s1)
    (set-register! $p2 s2)
    (jump loop:))

  (defblock loop:
    (let b1 (pointer-read (Bitvector 8) le $p1))
    (let b2 (pointer-read (Bitvector 8) le $p2))
    (let bytes-eq (equal? b1 b2))
    (branch bytes-eq check-null: differ:))

  (defblock check-null:
    (let cn-b1 (pointer-read (Bitvector 8) le $p1))
    (let cn-null (equal? cn-b1 (bv 8 0)))
    (branch cn-null equal: advance:))

  (defblock advance:
    (let adv-p1 (pointer-add $p1 (bv 64 1)))
    (let adv-p2 (pointer-add $p2 (bv 64 1)))
    (set-register! $p1 adv-p1)
    (set-register! $p2 adv-p2)
    (jump loop:))

  (defblock equal:
    (let res-zero (bits-to-pointer (bv 64 0)))
    (return res-zero))

  (defblock differ:
    (let res-one (bits-to-pointer (bv 64 1)))
    (return res-one)))
