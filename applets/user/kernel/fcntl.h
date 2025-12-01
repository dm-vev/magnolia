#pragma once
#include <fcntl.h>

#ifndef O_RDONLY
#define O_RDONLY 0x0000
#endif
#ifndef O_WRONLY
#define O_WRONLY 0x0001
#endif
#ifndef O_RDWR
#define O_RDWR   0x0002
#endif
#ifndef O_CREAT
#define O_CREAT  0x0200
#endif
#ifndef O_TRUNC
#define O_TRUNC  0x0400
#endif
#ifndef O_CREATE
#define O_CREATE O_CREAT
#endif
