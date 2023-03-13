

/* Define to the attribute for enabling parameter checks on printf-like functions. */
#define PRINTF_FORMAT(a, b) __attribute__ ((__format__ (__printf__, a, b)))
