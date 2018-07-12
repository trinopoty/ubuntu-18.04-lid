#include <dirent.h>
#include <malloc.h>
#include <memory.h>
#include <fcntl.h>
#include <unistd.h>
#include <asm/errno.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <systemd/sd-bus.h>

#include "basic.h"
#include "lidManager.h"
#include "button.h"

static int on_ac_power() {
    bool found_offline = false, found_online = false;
    _cleanup_(closedirp) DIR *d = NULL;
    struct dirent *de;

    d = opendir("/sys/class/power_supply");
    if (!d) {
        return true;
    }

    int errno;
    FOREACH_DIRENT(de, d, return true) {
        _cleanup_(closep) int fd, device;
        char contents[6];
        ssize_t n;

        device = openat(dirfd(d), de->d_name, O_DIRECTORY|O_RDONLY|O_CLOEXEC|O_NOCTTY);
        if (device < 0) {
            return -1;
        }

        fd = openat(device, "type", O_RDONLY|O_CLOEXEC|O_NOCTTY);
        if (fd < 0) {
            continue;
        }

        n = read(fd, contents, sizeof(contents));
        if (n < 0) {
            return -1;
        }

        if (n != 6 || memcmp(contents, "Mains\n", 6) != 0)
            continue;

        close(fd);

        fd = openat(device, "online", O_RDONLY|O_CLOEXEC|O_NOCTTY);
        if (fd < 0) {
            return -1;
        }

        n = read(fd, contents, sizeof(contents));
        if (n < 0) {
            return -1;
        }

        if (n != 2 || contents[1] != '\n')
            return -1;

        if (contents[0] == '1') {
            found_online = true;
            break;
        } else if (contents[0] == '0') {
            found_offline = true;
        } else {
            return -1;
        }
    }

    return (found_online || !found_offline)? 1 : 0;
}

static void handle_lid_closed(LidManager* lidManager) {
    int on_ac = on_ac_power();
    if (on_ac >= 0) {
        if (on_ac == 0) {
            // Lock and Suspend
            sd_bus_error bus_error = {};
            sd_bus_message* bus_reply = NULL;
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
                if (sd_bus_call_method(lidManager->system_bus,
                                       "org.freedesktop.login1",
                                       "/org/freedesktop/login1",
                                       "org.freedesktop.login1.Manager",
                                       "Suspend",
                                       &bus_error,
                                       &bus_reply,
                                       "b",
                                       0) < 0) {
                    // Do nothing
                }
            }
        } else {
            // Lock
            sd_bus_error bus_error = {};
            sd_bus_message* bus_reply = NULL;
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
            }
        }
    }
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static int button_handler(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
#pragma clang diagnostic pop

    Button* button = (Button*) userdata;
    struct input_event ev;
    ssize_t l;

    l = read(button->fd, &ev, sizeof(ev));
    if (l == 0) {
        return 0;
    }
    if (l < sizeof(ev)) {
        return 0;
    }

    if (ev.type == EV_SW && ev.value > 0) {
        if (ev.code == SW_LID) {
            button->lid_closed = true;
            handle_lid_closed(button->manager);
        }
    } else if (ev.type == EV_SW && ev.value == 0) {
        if (ev.code == SW_LID) {
            button->lid_closed = false;
        }
    }

    return 0;
}

bool button_is_lid(Button* button) {
    int fd = button->fd;

    unsigned long types[EV_SW/ULONG_BITS+1];
    if (ioctl(fd, EVIOCGBIT(EV_SYN, sizeof(types)), types) < 0)
        return false;

    if (bitset_get(types, EV_SW)) {
        unsigned long switches[SW_LID/ULONG_BITS+1];

        if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof(switches)), switches) < 0)
            return false;

        if (bitset_get(switches, SW_LID))
            return true;
    }

    return false;
}

int button_set_mask(Button* button) {
    int fd = button->fd;

    unsigned long
            types[EV_SW/ULONG_BITS+1] = {},
            switches[SW_LID/ULONG_BITS+1] = {};
    struct input_mask mask;

    bitset_put(types, EV_KEY);
    bitset_put(types, EV_SW);

    mask = (struct input_mask) {
            .type = EV_SYN,
            .codes_size = sizeof(types),
            .codes_ptr = PTR_TO_UINT64(types),
    };

    if (ioctl(fd, EVIOCSMASK, &mask) < 0)
        return -1;

    bitset_put(switches, SW_LID);
    bitset_put(switches, SW_DOCK);

    mask = (struct input_mask) {
            .type = EV_SW,
            .codes_size = sizeof(switches),
            .codes_ptr = PTR_TO_UINT64(switches),
    };

    if (ioctl(fd, EVIOCSMASK, &mask) < 0)
        return -1;

    return 0;
}

Button* button_new(LidManager* manager, const char* name) {
    Button* button = malloc(sizeof(Button));
    memset(button, 0, sizeof(Button));

    button->manager = manager;
    button->name = strdup(name);
    button->fd = -1;

    return button;
}

int button_open(Button* button) {
    const char* inputDevicePrefix = "/dev/input/";
    const size_t len = strlen(inputDevicePrefix) + strlen(button->name) + 1;
    _cleanup_(freep) char* deviceFile = malloc(len);
    memset(deviceFile, 0, len);
    memcpy(deviceFile, inputDevicePrefix, strlen(inputDevicePrefix));
    memcpy(&deviceFile[strlen(inputDevicePrefix)], button->name, strlen(button->name));

    button->fd = open(deviceFile, O_RDWR|O_CLOEXEC|O_NOCTTY|O_NONBLOCK);
    if (button->fd < 0) {
        fprintf(stderr, "Unable to open device: %s", deviceFile);
        return -1;
    }

    if (button_is_lid(button) != true) {
        goto fail;
    }

    char name[256];
    if (ioctl(button->fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        goto fail;
    }

    button_set_mask(button);

    if (sd_event_add_io(button->manager->event, &button->io_event_source, button->fd, EPOLLIN, button_handler, button) < 0) {
        goto fail;
    }

    return 0;

    fail:
    return -1;
}

void button_close(Button* button) {
    if (button->io_event_source) {
        sd_event_source_unref(button->io_event_source);
    }
    if (button->fd) {
        close(button->fd);
    }

    free(button);
}

int button_create(LidManager* manager, const char* name, Button **pButton) {
    int r;
    Button* button = button_new(manager, name);
    if (!button) {
        return -ENOMEM;
    }

    r = button_open(button);
    if (r < 0) {
        goto fail;
    }

    *pButton = button;

    return 0;

    fail:

    button_close(button);

    return r;
}