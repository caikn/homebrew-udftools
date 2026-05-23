#ifndef __CONFIG_H
#define __CONFIG_H

#define PACKAGE_NAME "udftools"
#define PACKAGE_VERSION "2.3-macos"

/* macOS on Apple Silicon (arm64) is little-endian */
/* #undef WORDS_BIGENDIAN */

/* Enable large file support */
#define _FILE_OFFSET_BITS 64

#endif /* __CONFIG_H */
