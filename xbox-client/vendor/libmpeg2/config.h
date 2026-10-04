/* Xbox build configuration; upstream libmpeg2 0.5.1. */
#define ARCH_X86 1
#define HAVE_BUILTIN_EXPECT 1
#define ATTRIBUTE_ALIGNED_MAX 64
/* Do not probe CPUs with signals: Xbox is Pentium III (MMX + MMXEXT).
 * The caller explicitly selects those capabilities, never SSE2. */
