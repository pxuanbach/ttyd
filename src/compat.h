/* compat.h -- MSVC compatibility shims for POSIX functions */
#ifndef TTYD_COMPAT_H
#define TTYD_COMPAT_H

#ifdef _MSC_VER
#include <string.h>
#include <sys/stat.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
#define rmdir _rmdir

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#endif

// O_BINARY is MSVC/Windows-only (forces raw binary mode for open()).
// On POSIX systems the flag is unnecessary; default it to 0 so existing
// code that uses O_BINARY unconditionally compiles everywhere.
#ifndef O_BINARY
#define O_BINARY 0
#endif

#endif /* TTYD_COMPAT_H */
