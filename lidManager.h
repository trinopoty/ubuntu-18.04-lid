//
// Created by root on 12/7/18.
//

#ifndef SYSTEMD_LID_LID_H
#define SYSTEMD_LID_LID_H

#include <libudev.h>
#include <gio/gio.h>

struct LidManager;
struct Button;
struct Power;

typedef struct LidManager {
    struct udev* udev;

    GMainLoop *loop;
    GDBusConnection *connection;

    struct Button* button;
    struct Power* power;
} LidManager;

typedef void (*lidManager_handler)(const LidManager* lidManager);

int lidManager_new(LidManager** pLidManager);
void lidManager_close(LidManager* lidManager);

#endif //SYSTEMD_LID_LID_H
