#ifndef SYSTEMD_LID_POWER_H
#define SYSTEMD_LID_POWER_H

#include <stdbool.h>
#include <systemd/sd-event.h>

#include "lidManager.h"

struct Power;

typedef struct Power {
    const struct LidManager* manager;
    const char* devName;
    const char* sysPath;
    lidManager_handler handler;

    sd_event_source* io_event_source;

    int udev_fd;
    struct udev_monitor* udev_monitor;

    bool ac_connected;
} Power;

Power* power_new(struct LidManager* lidManager, const char* devName, const char* sysPath, lidManager_handler handler);
int power_open(Power* power);
void power_close(Power* power);
int power_create(struct LidManager* lidManager, Power** pPower, const char* devName, const char* sysPath, lidManager_handler handler);

#endif //SYSTEMD_LID_POWER_H
