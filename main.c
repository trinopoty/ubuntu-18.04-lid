#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <unistd.h>
#include <libudev.h>
#include <asm/errno.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>

#include "basic.h"
#include "lidManager.h"
#include "button.h"
#include "power.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static int sig_int_handler(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
#pragma clang diagnostic pop

    LidManager* lidManager = (LidManager*) userdata;
    sd_event_exit(lidManager->event, 0);
}

static void lidManager_handler_impl(const LidManager* lidManager) {
    bool ac_connected = ((lidManager->power == NULL) || lidManager->power->ac_connected);
    bool lid_closed = (lidManager->button && lidManager->button->lid_closed);

    if (lid_closed) {
        if (ac_connected) {
            // Lock
            sd_bus_error bus_error = {};
            sd_bus_message *bus_reply = NULL;
            sd_bus_call_method(lidManager->system_bus,
                               "org.freedesktop.login1",
                               "/org/freedesktop/login1",
                               "org.freedesktop.login1.Manager",
                               "LockSessions",
                               &bus_error,
                               &bus_reply,
                               "",
                               0);
        } else {
            // Lock and Suspend
            sd_bus_error bus_error = {};
            sd_bus_message *bus_reply = NULL;
            if (sd_bus_call_method(lidManager->system_bus,
                                   "org.freedesktop.login1",
                                   "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager",
                                   "LockSessions",
                                   &bus_error,
                                   &bus_reply,
                                   "",
                                   0) < 0) {
                // Do nothing
            } else {
                sd_bus_call_method(lidManager->system_bus,
                                   "org.freedesktop.login1",
                                   "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager",
                                   "Suspend",
                                   &bus_error,
                                   &bus_reply,
                                   "b",
                                   0);
            }
        }
    }
}

int find_lid(LidManager *lidManager) {
    _cleanup_(udev_enumerate_unrefp) struct udev_enumerate *e = NULL;
    int r;

    e = udev_enumerate_new(lidManager->udev);
    if (!e)
        return -1;

    r = udev_enumerate_add_match_subsystem(e, "input");
    if (r < 0)
        return r;

    r = udev_enumerate_add_match_tag(e, "power-switch");
    if (r < 0)
        return r;

    r = udev_enumerate_add_match_is_initialized(e);
    if (r < 0)
        return r;

    r = udev_enumerate_scan_devices(e);
    if (r < 0)
        return r;

    struct udev_list_entry *item = NULL, *first = NULL;
    first = udev_enumerate_get_list_entry(e);

    udev_list_entry_foreach(item, first) {
        _cleanup_(udev_device_unrefp) struct udev_device *d = NULL;
        d = udev_device_new_from_syspath(lidManager->udev, udev_list_entry_get_name(item));
        if (!d)
            return -1;

        const char* name = udev_device_get_sysname(d);
        Button* button;
        if (button_create(lidManager, &button, name, lidManager_handler_impl) >= 0) {
            lidManager->button = button;
            break;
        }
    }

    return (lidManager->button)? 1 : 0;
}

int find_ac_adapter(LidManager* lidManager) {
    _cleanup_(closedirp) DIR *d = NULL;
    struct dirent *de;

    d = opendir("/sys/class/power_supply");
    if (!d) {
        return -ENOENT;
    }

    int errno;
    FOREACH_DIRENT(de, d, return -EIO) {
            _cleanup_(closep) int fd, device;
            char contents[6];
            ssize_t n;

            device = openat(dirfd(d), de->d_name, O_DIRECTORY|O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (device < 0) {
                return -ENOENT;
            }

            fd = openat(device, "type", O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (fd < 0) {
                continue;
            }

            n = read(fd, contents, sizeof(contents));
            if (n < 0) {
                return -EIO;
            }

            if (n != 6 || memcmp(contents, "Mains\n", 6) != 0)
                continue;

            close(fd);

            fd = openat(device, "online", O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (fd < 0) {
                return -1;
            }

            // Create the directory path
            const char* dirName = de->d_name;
            const char* dirPrefix = "/sys/class/power_supply/";
            const size_t len = strlen(dirPrefix) + strlen(dirName) + 1;
            _cleanup_(freep) char* dirPath = malloc(len);
            memset(dirPath, 0, len);
            memcpy(dirPath, dirPrefix, strlen(dirPrefix));
            memcpy(&dirPath[strlen(dirPrefix)], dirName, strlen(dirName));

            // Found a mains supply
            Power* power = NULL;
            if (power_create(lidManager, &power, dirName, dirPath, lidManager_handler_impl) >= 0) {
                lidManager->power = power;
                break;
            }
        }

    return errno;
}

int main() {
    LidManager* lidManager = NULL;
    if (lidManager_new(&lidManager) < 0) {
        goto exit;
    }

    if (find_lid(lidManager) >= 0) {
        find_ac_adapter(lidManager);
    }

    sd_bus_error bus_error = {};
    sd_bus_message* bus_reply = NULL;
    if (sd_bus_call_method(lidManager->system_bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1",
                           "org.freedesktop.login1.Manager",
                           "Inhibit",
                           &bus_error,
                           &bus_reply,
                           "ssss",
                           "handle-lid-switch", "ubuntu-lid-fixer", "user preference", "block") < 0) {
        goto exit;
    }

    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGINT);
    sigaddset(&ss, SIGTERM);

    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0) {
        goto exit;
    }

    if (sd_event_add_signal(lidManager->event, NULL, SIGINT, sig_int_handler, lidManager) < 0) {
        goto exit;
    }
    if (sd_event_add_signal(lidManager->event, NULL, SIGTERM, sig_int_handler, lidManager) < 0) {
        goto exit;
    }

    sd_event_loop(lidManager->event);

    exit:
    if (lidManager) {
        lidManager_close(lidManager);
    }

    return 0;
}