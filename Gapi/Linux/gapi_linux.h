/*
 * gapi_linux.h -- shared inside the Linux GAPI backend.
 */
#ifndef GAPI_LINUX_H
#define GAPI_LINUX_H

/* the text gapi_last_error gives (printf style) */
void gapi_linux_set_error(const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

#endif /* GAPI_LINUX_H */
