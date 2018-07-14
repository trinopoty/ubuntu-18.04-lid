#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>
#include <libudev.h>
#include <asm/errno.h>
#include <linux/input.h>

#include "basic.h"
#include "lidManager.h"
#include "power.h"

static int detect_ac_connected(Power* power) {
    _cleanup_(closedirp) DIR *sysDir = NULL;

    sysDir = opendir(power->sysPath);
    if (!sysDir) {
        return -ENOENT;
    }

    _cleanup_(closep) int fd = openat(dirfd(sysDir), "online", O_RDONLY|O_CLOEXEC|O_NOCTTY);
    if (!fd) {
        fprintf(stderr, "Unable to open: %s/%s\n", power->sysPath, "online");
        return -ENOENT;
    }

    char contents[5] = {};
    ssize_t n;

    n = read(fd, contents, sizeof(contents));
    if (n < 0) {
        return -1;
    }

    if (n != 2 || contents[1] != '\n') {
        return -1;
    }

    return (contents[0] == '1')? 1 : 0;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
static int ac_adapter_handler(sd_event_source *s, int fd, uint32_t revents, void *userdata) {
#pragma clang diagnostic pop

    Power* power = (Power*) userdata;

    struct udev_device* device = udev_monitor_receive_device(power->udev_monitor);
    if (device) {
        const char* devName = udev_device_get_sysname(device);
        if (devName && strcmp(devName, power->devName) == 0) {
            power->ac_connected = (detect_ac_connected(power) == 1);
            power->handler(power->manager);
        }
        udev_device_unref(device);
    }
}

Power* power_new(struct LidManager* lidManager, const char* devName, const char* sysPath, lidManager_handler handler) {
    Power* power = malloc(sizeof(Power));
    memset(power, 0, sizeof(Power));

    power->manager = lidManager;
    power->devName = strdup(devName);
    power->sysPath = strdup(sysPath);
    power->handler = handler;

    power->io_event_source = NULL;
    power->udev_fd = -1;
    power->ac_connected = false;

    return power;
}

int power_open(Power* power) {
    int r;
    struct udev_monitor* udev_monitor = udev_monitor_new_from_netlink(power->manager->udev, "udev");
    if (!udev_monitor) {
        return -ENOMEM;
    }

    r = udev_monitor_set_receive_buffer_size(udev_monitor, 1024*1024);
    if (r < 0) {
        return r;
    }

    int fd_udev = udev_monitor_get_fd(udev_monitor);
    if (fd_udev < 0) {
        return fd_udev;
    }

    power->udev_fd = fd_udev;

    r = udev_monitor_filter_add_match_subsystem_devtype(udev_monitor,
                                                        "power_supply",
                                                        NULL);
    if (r < 0) {
        return r;
    }

    r = udev_monitor_enable_receiving(udev_monitor);
    if (r < 0) {
        return r;
    }

    r = sd_event_add_io(power->manager->event, &power->io_event_source, fd_udev, EPOLLIN, ac_adapter_handler, power);
    if (r < 0) {
        return r;
    }

    power->udev_monitor = udev_monitor;

    return 0;
}

void power_close(Power* power) {
    if (power->io_event_source) {
        sd_event_source_unref(power->io_event_source);
    }
    if (power->udev_fd > 0) {
        close(power->udev_fd);
    }

    free(power);
}

int power_create(struct LidManager* lidManager, Power** pPower, const char* devName, const char* sysPath, lidManager_handler handler) {
    int r;
    Power* power = power_new(lidManager, devName, sysPath, handler);
    if (!power) {
        return -ENOMEM;
    }

    r = power_open(power);
    if (r < 0) {
        goto fail;
    }

    power->ac_connected = (detect_ac_connected(power) == 1);

    *pPower = power;

    return 0;

    fail:

    power_close(power);

    return r;
}
