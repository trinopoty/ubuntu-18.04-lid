#include <memory.h>
#include <libudev.h>
#include <asm/errno.h>

#include "lidManager.h"
#include "button.h"

int lidManager_new(LidManager** pLidManager) {
    LidManager* lidManager = malloc(sizeof(LidManager));
    memset(lidManager, 0, sizeof(LidManager));

    if (!lidManager) {
        return -ENOMEM;
    }

    lidManager->udev = udev_new();
    if (!lidManager->udev) {
        return -ENOMEM;
    }

    *pLidManager = lidManager;

    return 0;
}

void lidManager_close(LidManager* lidManager) {
    if (lidManager->button) {
        button_close(lidManager->button);
    }

    if (lidManager->udev) {
        udev_unref(lidManager->udev);
    }

    free(lidManager);
}