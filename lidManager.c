#include <malloc.h>
#include <memory.h>
#include <libudev.h>
#include <stdbool.h>
#include <asm/errno.h>

#include "basic.h"
#include "lidManager.h"
#include "button.h"

int lidManager_new(LidManager** pLidManager) {
    int r;

    LidManager* lidManager = malloc(sizeof(LidManager));
    memset(lidManager, 0, sizeof(LidManager));

    if (!lidManager) {
        return -ENOMEM;
    }

    lidManager->udev = udev_new();
    if (!lidManager->udev) {
        return -ENOMEM;
    }

    r = sd_event_new(&lidManager->event);
    if (r < 0) {
        return r;
    }

    r = sd_event_set_watchdog(lidManager->event, true);
    if (r < 0) {
        return r;
    }

    r = sd_bus_open_system(&lidManager->system_bus);
    if (r < 0) {
        return r;
    }

    *pLidManager = lidManager;

    return 0;
}

void lidManager_close(LidManager* lidManager) {
    if (lidManager->button) {
        button_close(lidManager->button);
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
}