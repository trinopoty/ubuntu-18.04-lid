#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <malloc.h>
#include <memory.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <linux/input.h>
#include <libudev.h>
#include <systemd/sd-event.h>
#include <systemd/sd-bus.h>

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

struct udev_enumerate *udev_enumerate_unrefp(struct udev_enumerate **udev_enumerate) {
    return udev_enumerate_unref(*udev_enumerate);
}

struct udev_device *udev_device_unrefp(struct udev_device **udev_device) {
    return udev_device_unref(*udev_device);
}

int closep(const int* fd) {
    return close(*fd);
}

int closedirp(DIR **d) {
    return closedir(*d);
}

static bool bitset_get(const unsigned long *bits, unsigned i) {
    return (bits[i / ULONG_BITS] >> (i % ULONG_BITS)) & 1UL;
}

static void bitset_put(unsigned long *bits, unsigned i) {
    bits[i / ULONG_BITS] |= (unsigned long) 1 << (i % ULONG_BITS);
}

struct LidManager;
struct Button;

typedef struct Button {
    struct LidManager* manager;
    char* name;
    int fd;

    sd_event_source* io_event_source;

    bool lib_closed;
} Button;

typedef struct LidManager {
    struct udev* udev;
    sd_event *event;
    sd_bus* system_bus;

    int buttonCount;
    Button** buttons;
} LidManager;

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
            _cleanup_(closep) int fd = -1, device = -1;
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

            if (n != 6 || memcmp(contents, "Mains\n", 6))
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

static int sig_int_handler(sd_event_source *s, const struct signalfd_siginfo *si, void *userdata) {
    LidManager* lidManager = (LidManager*) userdata;
    sd_event_exit(lidManager->event, 0);
}

static int button_handler(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
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
            button->lib_closed = true;
            handle_lid_closed(button->manager);
        }
    } else if (ev.type == EV_SW && ev.value == 0) {
        if (ev.code == SW_LID) {
            button->lib_closed = false;
        }
    }

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

bool button_is_lid(int fd) {
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

int button_set_mask(int fd) {
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

int button_open(Button* button) {
    const char* inputDevicePrefix = "/dev/input/";
    const size_t len = strlen(inputDevicePrefix) + strlen(button->name) + 1;
    char* deviceFile = malloc(len);
    memset(deviceFile, 0, len);
    memcpy(deviceFile, inputDevicePrefix, strlen(inputDevicePrefix));
    memcpy(&deviceFile[strlen(inputDevicePrefix)], button->name, strlen(button->name));

    button->fd = open(deviceFile, O_RDWR|O_CLOEXEC|O_NOCTTY|O_NONBLOCK);
    if (button->fd < 0) {
        fprintf(stderr, "Unable to open device: %s", deviceFile);
        return -1;
    }

    if (button_is_lid(button->fd) != true) {
        goto fail;
    }

    char name[256];
    if (ioctl(button->fd, EVIOCGNAME(sizeof(name)), name) < 0) {
        goto fail;
    }

    button_set_mask(button->fd);

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

int enumerate_power_switches(LidManager* lidManager) {
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

    int deviceCount = 0;
    udev_list_entry_foreach(item, first) {
        deviceCount += 1;
    }

    lidManager->buttonCount = 0;
    lidManager->buttons = malloc(sizeof(Button) * deviceCount);
    memset(lidManager->buttons, 0, sizeof(Button) * deviceCount);

    int buttonIdx = 0;
    udev_list_entry_foreach(item, first) {
        _cleanup_(udev_device_unrefp) struct udev_device *d = NULL;
        int k;

        d = udev_device_new_from_syspath(lidManager->udev, udev_list_entry_get_name(item));
        if (!d)
            return -1;

        const char* name = udev_device_get_sysname(d);
        Button* button = button_new(lidManager, name);
        k = button_open(button);
        if (k >= 0) {
            lidManager->buttons[buttonIdx] = button;
            buttonIdx += 1;
            lidManager->buttonCount += 1;
        } else {
            button_close(button);
        }
    }
}

int main() {
    LidManager* lidManager = malloc(sizeof(LidManager));
    memset(lidManager, 0, sizeof(LidManager));

    lidManager->udev = udev_new();

    if (sd_event_new(&lidManager->event) < 0) {
        goto exit;
    }
    sd_event_set_watchdog(lidManager->event, true);

    if (sd_bus_open_system(&lidManager->system_bus) < 0) {
        goto exit;
    }

    enumerate_power_switches(lidManager);

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
    if (lidManager->buttons && lidManager->buttonCount > 0) {
        for (int i = 0; i < lidManager->buttonCount; i++) {
            button_close(lidManager->buttons[i]);
        }
        free(lidManager->buttons);
    }

    if (lidManager->system_bus) {
        sd_bus_unref(lidManager->system_bus);
    }
    if (lidManager->event) {
        sd_event_unref(lidManager->event);
    }
    if (lidManager->udev) {
        udev_unref(lidManager->udev);
    }

    free(lidManager);

    return 0;
}