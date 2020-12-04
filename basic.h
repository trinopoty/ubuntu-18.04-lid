//
// Created by root on 12/7/18.
//

#ifndef SYSTEMD_LID_BASIC_H
#define SYSTEMD_LID_BASIC_H

#include <dirent.h>
#include <stdbool.h>
#include <unistd.h>
#include <libudev.h>
#include <stdint.h>

#define _cleanup_(x) __attribute__((cleanup(x)))

#define ULONG_BITS (sizeof(unsigned long)*8)
#define PTR_TO_UINT64(p) ((uint64_t) ((uintptr_t) (p)))

#define FOREACH_DIRENT(de, d, on_error)                                 \
        for (errno = 0, de = readdir(d);; errno = 0, de = readdir(d))   \
                if (!de) {                                              \
                        if (errno > 0) {                                \
                                on_error;                               \
                        }                                               \
                        break;                                          \
                } else

static bool bitset_get(const unsigned long *bits, unsigned i) {
    return (bits[i / ULONG_BITS] >> (i % ULONG_BITS)) & 1UL;
}

static void bitset_put(unsigned long *bits, unsigned i) {
    bits[i / ULONG_BITS] |= (unsigned long) 1 << (i % ULONG_BITS);
}

static struct udev_enumerate *udev_enumerate_unrefp(struct udev_enumerate **udev_enumerate) {
    return udev_enumerate_unref(*udev_enumerate);
}

static struct udev_device *udev_device_unrefp(struct udev_device **udev_device) {
    return udev_device_unref(*udev_device);
}

static void freep(void **mem) {
    free(*mem);
}

static int closep(const int* fd) {
    return close(*fd);
}

static int closedirp(DIR **d) {
    return closedir(*d);
}

#endif //SYSTEMD_LID_BASIC_H
