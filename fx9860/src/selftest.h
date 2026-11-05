/* fxj2me device: on-calc VM self-test (diagnostics screen). */
#ifndef FXJ2ME_SELFTEST_H
#define FXJ2ME_SELFTEST_H

/* Boot the VM against the embedded test jar, run the interpreter battery and
 * show results. Returns to the caller when the user presses EXIT. All memory
 * is released before returning. */
void selftest_run(void);

/* Find the largest block malloc() can give (2k-granular, 4k..384k), split to
 * leave at least `class_reserve` bytes for the class-loading mallocs that
 * follow (at most half the block). Returns the block (caller frees) or NULL;
 * *out receives its size. Shared with the MIDlet run screen. */
void *probe_largest(size_t *out, size_t class_reserve);

#endif /* FXJ2ME_SELFTEST_H */
