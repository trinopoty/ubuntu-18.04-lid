#ifndef SYSTEMD_LID_BUTTON_H
#define SYSTEMD_LID_BUTTON_H

#include <stdbool.h>
#include <systemd/sd-event.h>

#include "lidManager.h"

struct Button;

typedef struct Button {
    const struct LidManager* manager;
    const char* name;
    lidManager_handler handler;

    int fd;

    sd_event_source* io_event_source;

    bool lid_closed;
} Button;

bool button_is_lid(Button* button);
int button_set_mask(Button* button);

Button* button_new(LidManager* manager, const char* name, lidManager_handler handler);
int button_open(Button* button);
void button_close(Button* button);
int button_create(LidManager* lidManager, Button **pButton, const char* name, lidManager_handler handler);

#endif //SYSTEMD_LID_BUTTON_H
