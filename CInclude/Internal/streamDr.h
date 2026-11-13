/*
 * streamDr.h -- the functions of a stream driver (the C form of
 * Include/Internal/streamDr.def), for the callers of a stream driver's
 * strategy routine (geosdriver.h) and the stream drivers in C.
 *
 * The register use is that of streamDr.def; a far pointer is DR_dsSi.
 */
#ifndef STREAMDR_H
#define STREAMDR_H

#include <driver.h>

#define DR_STREAM_GET_DEVICE_MAP    8
#define DR_STREAM_CREATE            10
#define DR_STREAM_OPEN              10
#define DR_STREAM_DESTROY           12
#define DR_STREAM_CLOSE             12
#define DR_STREAM_SET_NOTIFY        14
#define DR_STREAM_GET_ERROR         16
#define DR_STREAM_SET_ERROR         18
#define DR_STREAM_FLUSH             20
#define DR_STREAM_SET_THRESHOLD     22
#define DR_STREAM_READ              24
#define DR_STREAM_READ_BYTE         26
#define DR_STREAM_WRITE             28
#define DR_STREAM_WRITE_BYTE        30
#define DR_STREAM_QUERY             32

#define STREAM_ESC_LOAD_OPTIONS     0x8100
#define STREAM_ESC_RESTART_OUTPUT   0x8101

#endif /* STREAMDR_H */
