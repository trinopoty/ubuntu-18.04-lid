#ifndef SYSTEMD_LID_BUTTON_H
#define SYSTEMD_LID_BUTTON_H

#include <stdbool.h>
#include <systemd/sd-event.h>

struct LidManager;
struct Button;

typedef struct Button {
    struct LidManager* manager;
    char* name;
    int fd;

    sd_event_source* io_event_source;

    bool lid_closed;
} Button;

bool button_is_lid(Button* button);
int button_set_mask(Button* button);

Button* button_new(LidManager* manager, const char* name);
int button_open(Button* button);
void button_close(Button* button);
int button_create(LidManager* manager, const char* name, Button **pButton);

#endif //SYSTEMD_LID_BUTTON_H
