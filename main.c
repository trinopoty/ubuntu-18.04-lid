#include <fcntl.h>
#include <malloc.h>
#include <memory.h>
#include <unistd.h>
#include <libudev.h>
#include <asm/errno.h>
#include <dconf/dconf.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <systemd/sd-event.h>
#include <gio/gio.h>
#include <glib-unix.h>

#include "basic.h"
#include "lidManager.h"
#include "button.h"
#include "power.h"

typedef void (*handler_func)(const LidManager* lidManager);

static gboolean sig_int_handler(gpointer user_data) {
    LidManager* lidManager = (LidManager*) user_data;
    g_main_loop_quit(lidManager->loop);

    return G_SOURCE_CONTINUE;
}

static void handler_nothing(const LidManager* lidManager) {
}

/**
 * Lock
 *
 * @param lidManager
 */
static void handler_lock(const LidManager* lidManager) {
    GDBusConnection *connection = lidManager->connection;
    GError *error = NULL;

    GVariant *parameters = g_variant_new("()");
    GVariant *result = g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "ListSessions",
            parameters,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            10 * 1000,
            NULL,
            &error);
    if (!error) {
        GVariant *array = g_variant_get_child_value(result, 0);
        GVariantIter *arrayIter = g_variant_iter_new(array);
        char *session_name;
        guint32 session_uid;
        char *session_user;
        char *session_seat;
        char *session_path;
        int found_session = 0;

        __uid_t uid = getuid();

        while (g_variant_iter_loop(arrayIter, "(susso)", &session_name, &session_uid, &session_user, &session_seat,
                                   &session_path)) {
            if (uid == session_uid) {
                // Found our session
                found_session = 1;
                break;
            }
        }

        if (found_session) {
            parameters = g_variant_new("(s)", session_name);
            GVariant *result2 = g_dbus_connection_call_sync(
                    connection,
                    "org.freedesktop.login1",
                    "/org/freedesktop/login1",
                    "org.freedesktop.login1.Manager",
                    "LockSession",
                    parameters,
                    NULL,
                    G_DBUS_CALL_FLAGS_NONE,
                    10 * 1000,
                    NULL,
                    &error);
            if (result2) {
                g_variant_unref(result2);
            }
        }

        g_variant_iter_free(arrayIter);
        g_variant_unref(array);
        g_variant_unref(result);
    }
}

/**
 * Lock and suspend.
 *
 * @param lidManager
 */
static void handler_suspend(const LidManager* lidManager) {
    handler_lock(lidManager);

    GDBusConnection *connection = lidManager->connection;
    GError *error;

    GVariant *parameters = g_variant_new("(b)", FALSE);
    GVariant *result = g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "Suspend",
            parameters,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            10 * 1000,
            NULL,
            &error);
    if (result) {
        g_variant_unref(result);
    }
}

/**
 * Shutdown
 *
 * @param lidManager
 */
static void handler_shutdown(const LidManager* lidManager) {
    GDBusConnection *connection = lidManager->connection;
    GError *error;

    GVariant *parameters = g_variant_new("(b)", FALSE);
    GVariant *result = g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "PowerOff",
            parameters,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            10 * 1000,
            NULL,
            &error);
    if (result) {
        g_variant_unref(result);
    }
}

/**
 * Hibernate
 *
 * @param lidManager
 */
static void handler_hibernate(const LidManager* lidManager) {
    handler_lock(lidManager);

    GDBusConnection *connection = lidManager->connection;
    GError *error;

    GVariant *parameters = g_variant_new("(b)", FALSE);
    GVariant *result = g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "Hibernate",
            parameters,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            10 * 1000,
            NULL,
            &error);
    if (result) {
        g_variant_unref(result);
    }
}

/**
 * Logout
 *
 * @param lidManager
 */
static void handler_logout(const LidManager* lidManager) {
}

static void lidManager_handler_impl(const LidManager* lidManager) {
    bool ac_connected = ((lidManager->power == NULL) || lidManager->power->ac_connected);
    bool lid_closed = (lidManager->button && lidManager->button->lid_closed);

    if (lid_closed) {
        char* dconf_key = NULL;
        if (ac_connected) {
            dconf_key = "/org/gnome/settings-daemon/plugins/power/lid-close-ac-action";
        } else {
            dconf_key = "/org/gnome/settings-daemon/plugins/power/lid-close-battery-action";
        }

        DConfClient* dconf_client = dconf_client_new();
        GVariant* dconf_value = dconf_client_read(dconf_client, dconf_key);
        if (!dconf_value) {
            return;
        }

        gsize size;
        const gchar* value = g_variant_get_string(dconf_value, &size);
        if (!value) {
            return;
        }

        handler_func handler = NULL;

        if (strcmp(value, "blank") == 0) {
            // Blank is treated as lock because there is no "lock"
            handler = handler_lock;
        } else if (strcmp(value, "suspend") == 0) {
            handler = handler_suspend;
        } else if (strcmp(value, "shutdown") == 0) {
            handler = handler_shutdown;
        } else if (strcmp(value, "hibernate") == 0) {
            handler = handler_hibernate;
        } else if (strcmp(value, "logout") == 0) {
            handler = handler_logout;
        } else {
            handler = handler_nothing;
        }

        g_variant_unref(dconf_value);
        g_object_unref(dconf_client);

        handler(lidManager);
    }
}

int find_lid(LidManager *lidManager) {
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
        if (button_create(lidManager, &button, name, lidManager_handler_impl) >= 0) {
            lidManager->button = button;
            break;
        }
    }

    return (lidManager->button)? 1 : 0;
}

int find_ac_adapter(LidManager* lidManager) {
    _cleanup_(closedirp) DIR *d = NULL;
    struct dirent *de;

    d = opendir("/sys/class/power_supply");
    if (!d) {
        return -ENOENT;
    }

    int errno;
    FOREACH_DIRENT(de, d, return -EIO) {
            _cleanup_(closep) int fd, device;
            char contents[6];
            ssize_t n;

            device = openat(dirfd(d), de->d_name, O_DIRECTORY|O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (device < 0) {
                return -ENOENT;
            }

            fd = openat(device, "type", O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (fd < 0) {
                continue;
            }

            n = read(fd, contents, sizeof(contents));
            if (n < 0) {
                return -EIO;
            }

            close(fd);

            if (n != 6 || memcmp(contents, "Mains\n", 6) != 0)
                continue;

            fd = openat(device, "online", O_RDONLY|O_CLOEXEC|O_NOCTTY);
            if (fd < 0) {
                return -1;
            }

            // Create the directory path
            const char* dirName = de->d_name;
            const char* dirPrefix = "/sys/class/power_supply/";
            const size_t len = strlen(dirPrefix) + strlen(dirName) + 1;
            _cleanup_(freep) char* dirPath = malloc(len);
            memset(dirPath, 0, len);
            memcpy(dirPath, dirPrefix, strlen(dirPrefix));
            memcpy(&dirPath[strlen(dirPrefix)], dirName, strlen(dirName));

            // Found a mains supply
            Power* power = NULL;
            if (power_create(lidManager, &power, dirName, dirPath, lidManager_handler_impl) >= 0) {
                lidManager->power = power;
                break;
            }
        }

    return errno;
}

void on_connected(GDBusConnection *connection,
                  const gchar     *name,
                  const gchar     *name_owner,
                  gpointer         user_data) {
    LidManager *lidManager = (LidManager*) user_data;
    lidManager->connection = connection;

    GError *error = NULL;

    GVariant *parameters = g_variant_new("(ssss)", "handle-lid-switch", "ubuntu-lid-fixer", "user preference", "block");
    g_dbus_connection_call_sync(
            connection,
            "org.freedesktop.login1",
            "/org/freedesktop/login1",
            "org.freedesktop.login1.Manager",
            "Inhibit",
            parameters,
            NULL,
            G_DBUS_CALL_FLAGS_NONE,
            10 * 1000,
            NULL,
            &error);
    if (error) {
        g_dbus_connection_close_sync(connection, NULL, &error);
    }
}

void on_disconnected(GDBusConnection *connection,
                     const gchar     *name,
                     gpointer         user_data) {
    LidManager *lidManager = (LidManager*) user_data;
    lidManager->connection = NULL;
    g_main_loop_quit(lidManager->loop);
}

int main() {
    LidManager* lidManager = NULL;
    if (lidManager_new(&lidManager) < 0) {
        goto exit;
    }

    if (find_lid(lidManager) > 0) {
        find_ac_adapter(lidManager);
    }

    GMainLoop *loop = g_main_loop_new(NULL, FALSE);
    lidManager->loop = loop;

    g_unix_signal_add(SIGINT, sig_int_handler, lidManager);
    g_unix_signal_add(SIGTERM, sig_int_handler, lidManager);

    guint watcher_id = g_bus_watch_name(
            G_BUS_TYPE_SYSTEM,
            "org.freedesktop.login1",
            G_BUS_NAME_WATCHER_FLAGS_NONE,
            on_connected,
            on_disconnected,
            lidManager,
            NULL);
    g_main_loop_run(loop);
    g_bus_unwatch_name(watcher_id);

    exit:
    if (lidManager) {
        lidManager_close(lidManager);
    }

    return 0;
}