; Override for crc32(crc, buf, len) -> unsigned long
; Returns the input crc unchanged to avoid expensive symbolic CRC computation.
; The CRC value doesn't affect the vulnerability path (only used for validation).

(defun @crc32 ((crc (Ptr 64)) (buf (Ptr 64)) (len (Ptr 64))) (Ptr 64)
  (start start:
    (return crc)))
