#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <asm/errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <linux/input.h>
#include <libudev.h>
#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>

#include "basic.h"
#include "lidManager.h"
#include "button.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static int sig_int_handler(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
#pragma clang diagnostic pop

    LidManager* lidManager = (LidManager*) userdata;
    sd_event_exit(lidManager->event, 0);
}

int enumerate_lid(LidManager *lidManager) {
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
        if (button_create(lidManager, name, &button) >= 0) {
            lidManager->button = button;
            break;
        }
    }

    return (lidManager->button)? 1 : 0;
}

int main() {
    LidManager* lidManager = NULL;
    if (lidManager_new(&lidManager) < 0) {
        goto exit;
    }

    enumerate_lid(lidManager);

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