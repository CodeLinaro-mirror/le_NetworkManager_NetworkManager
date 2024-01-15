/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2023 Red Hat, Inc.
 */

#include "libnm-glib-aux/nm-default-glib-i18n-lib.h"

#include "nm-devlink.h"

#include <linux/if.h>
#include <linux/devlink.h>

#include "libnm-log-core/nm-logging.h"
#include "libnm-platform/nm-netlink.h"
#include "libnm-platform/nm-platform.h"
#include "libnm-platform/nm-platform-utils.h"

#define _NMLOG_PREFIX_NAME "devlink"
#define _NMLOG_DOMAIN      LOGD_PLATFORM | LOGD_DEVICE
#define _NMLOG(level, ...)                                                                        \
    G_STMT_START                                                                                  \
    {                                                                                             \
        char        _ifname_buf[IFNAMSIZ];                                                        \
        const char *_ifname = self ? nmp_utils_if_indextoname(self->ifindex, _ifname_buf) : NULL; \
                                                                                                  \
        nm_log((level),                                                                           \
               _NMLOG_DOMAIN,                                                                     \
               _ifname ?: NULL,                                                                   \
               NULL,                                                                              \
               "%s%s%s%s: " _NM_UTILS_MACRO_FIRST(__VA_ARGS__),                                   \
               _NMLOG_PREFIX_NAME,                                                                \
               NM_PRINT_FMT_QUOTED(_ifname, " (", _ifname, ")", "")                               \
                   _NM_UTILS_MACRO_REST(__VA_ARGS__));                                            \
    }                                                                                             \
    G_STMT_END

#define CB_RESULT_PENDING 0
#define CB_RESULT_OK      1

struct _NMDevlink {
    NMPlatform     *plat;
    struct nl_sock *genl_sock_sync;
    guint16         genl_family_id;
    int             ifindex;
};

/**
 * nm_devlink_new:
 * @platform: the #NMPlatform that will use this #NMDevlink instance
 * @genl_sock_sync: the netlink socket (will be used synchronously)
 * @ifindex: the kernel's netdev ifindex corresponding to the devlink device
 *
 * Create a new #NMDevlink instance to make devlink queries regarding a specific
 * device.
 *
 * Returns: (transfer full): the allocated new #NMDevlink
 */
NMDevlink *
nm_devlink_new(NMPlatform *platform, struct nl_sock *genl_sock_sync, int ifindex)
{
    NMDevlink *self      = g_new(NMDevlink, 1);
    self->plat           = platform;
    self->genl_sock_sync = genl_sock_sync;
    self->genl_family_id = nm_platform_genl_get_family_id(platform, NMP_GENL_FAMILY_TYPE_DEVLINK);
    self->ifindex        = ifindex;
    return self;
}

/**
 * nm_devlink_get_dev:
 * @self: the #NMDevlink
 * @out_bus: (out): the "bus_name" part of the devlink device identifier
 * @out_devname: (out): the "dev_name" part of the devlink device identifier
 * @error: (optional): the error location
 *
 * Get the devlink device identifier of the device for which the #NMDevlink was
 * created (with the @ifindex argument of nm_devlink_get_new()). A devlink device
 * is identified as "bus_name/dev_name" (i.e. "pci/0000:65:00.0"). This function
 * provides both parts separately.
 *
 * Note that here we only get the potential devlink device name. The real devlink
 * device might not even exist if the hw doesn't implement devlink or the netdev
 * doesn't have a 1-1 correspondig devlink device (i.e. because it's a VF or
 * because the hw uses a "one eswitch for many ports" model).
 *
 * Also note that currently only PCI devices are supported, an error will be
 * returned for other kind of devices.
 *
 * Returns: FALSE in case of error, TRUE otherwise
 */
gboolean
nm_devlink_get_dev(NMDevlink *self, char **out_bus, char **out_devname, GError **error)
{
    const char               *bus;
    char                      sbuf[IFNAMSIZ];
    NMPUtilsEthtoolDriverInfo ethtool_driver_info;

    nm_assert(out_bus != NULL && out_devname != NULL);

    if (!nm_platform_link_get_udev_property(self->plat, self->ifindex, "ID_BUS", &bus)) {
        g_set_error(error,
                    NM_UTILS_ERROR,
                    NM_UTILS_ERROR_UNKNOWN,
                    "Can't get udev info for device '%s'",
                    nmp_utils_if_indextoname(self->ifindex, sbuf));
        return FALSE;
    }

    if (!nm_streq0(bus, "pci")) {
        g_set_error_literal(error,
                            NM_UTILS_ERROR,
                            NM_UTILS_ERROR_UNKNOWN,
                            "Devlink is only supported for PCI");
        return FALSE;
    }

    if (!nmp_utils_ethtool_get_driver_info(self->ifindex, &ethtool_driver_info)) {
        g_set_error(error,
                    NM_UTILS_ERROR,
                    NM_UTILS_ERROR_UNKNOWN,
                    "Can't get ethtool driver info for device '%s'",
                    nmp_utils_if_indextoname(self->ifindex, sbuf));
        return FALSE;
    }

    *out_bus     = g_strdup("pci");
    *out_devname = g_strdup(ethtool_driver_info._private_bus_info);
    return TRUE;
}

static struct nl_msg *
devlink_alloc_msg(NMDevlink *self, uint8_t cmd, uint16_t flags)
{
    nm_auto_nlmsg struct nl_msg *msg = nlmsg_alloc(0);
    if (!msg)
        return NULL;

    genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, self->genl_family_id, 0, flags, cmd, 0);
    return g_steal_pointer(&msg);
}

static int
ack_cb_handler(const struct nl_msg *msg, void *data)
{
    int *result = data;
    *result     = CB_RESULT_OK;
    return NL_STOP;
}

static int
finish_cb_handler(const struct nl_msg *msg, void *data)
{
    int *result = data;
    *result     = CB_RESULT_OK;
    return NL_SKIP;
}

static int
err_cb_handler(const struct sockaddr_nl *nla, const struct nlmsgerr *err, void *data)
{
    void      **args       = data;
    NMDevlink  *self       = args[0];
    int        *result     = args[1];
    const char *extack_msg = NULL;

    *result = err->error;

    nlmsg_parse_error(&err->msg, &extack_msg);
    _LOGD("error response %s(%d)", extack_msg ?: "", err->error);

    return NL_SKIP;
}

static int
devlink_send_and_recv(NMDevlink     *self,
                      struct nl_msg *msg,
                      int (*valid_handler)(const struct nl_msg *, void *),
                      void *valid_data)
{
    int                nle;
    int                cb_result = CB_RESULT_PENDING;
    void              *err_arg[] = {self, &cb_result};
    const struct nl_cb cb        = {
               .err_cb     = err_cb_handler,
               .err_arg    = err_arg,
               .finish_cb  = finish_cb_handler,
               .finish_arg = &cb_result,
               .ack_cb     = ack_cb_handler,
               .ack_arg    = &cb_result,
               .valid_cb   = valid_handler,
               .valid_arg  = valid_data,
    };

    g_return_val_if_fail(msg != NULL, -ENOMEM);

    nle = nl_send_auto(self->genl_sock_sync, msg);
    if (nle < 0)
        return nle;

    while (cb_result == CB_RESULT_PENDING) {
        nle = nl_recvmsgs(self->genl_sock_sync, &cb);
        if (nle < 0 && nle != -EAGAIN) {
            _LOGW("nl_recvmsgs() error: (%d) %s", nle, nm_strerror(nle));
            break;
        }
    }

    if (nle >= 0 && cb_result < 0)
        nle = cb_result;
    return nle;
}

static int
devlink_parse_eswitch_mode(const struct nl_msg *msg, void *data)
{
    enum devlink_eswitch_mode *eswitch_mode = data;
    struct genlmsghdr         *gnlh         = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr             *tb[DEVLINK_ATTR_MAX + 1];

    if (nla_parse_arr(tb, genlmsg_attrdata(gnlh, 0), genlmsg_attrlen(gnlh, 0), NULL) < 0)
        return NL_SKIP;

    if (!tb[DEVLINK_ATTR_ESWITCH_MODE])
        return NL_SKIP;

    *eswitch_mode = nla_get_u16(tb[DEVLINK_ATTR_ESWITCH_MODE]);
    return NL_OK;
}

/*
 * nm_devlink_get_eswitch_mode:
 * @self: the #NMDevlink
 * @error: the error location
 *
 * Get the eswitch mode of the device related to the #NMDevlink instance. Note
 * that this might be unsupported by the device (see nm_devlink_get_dev()).
 *
 * Returns: the eswitch mode of the device, or <0 in case of error
 */
int
nm_devlink_get_eswitch_mode(NMDevlink *self, GError **error)
{
    nm_auto_nlmsg struct nl_msg *msg     = NULL;
    gs_free char                *bus     = NULL;
    gs_free char                *devname = NULL;
    enum devlink_eswitch_mode    eswitch_mode;

    if (!nm_devlink_get_dev(self, &bus, &devname, error))
        return -1;

    msg = devlink_alloc_msg(self, DEVLINK_CMD_ESWITCH_GET, 0);
    NLA_PUT_STRING(msg, DEVLINK_ATTR_BUS_NAME, bus);
    NLA_PUT_STRING(msg, DEVLINK_ATTR_DEV_NAME, devname);

    if (devlink_send_and_recv(self, msg, devlink_parse_eswitch_mode, &eswitch_mode) < 0) {
        g_set_error_literal(error,
                            NM_UTILS_ERROR,
                            NM_UTILS_ERROR_UNKNOWN,
                            "devlink: get-eswitch-mode: failed");
        return -1;
    }

    _LOGD("get-eswitch-mode: success");

    return (int) eswitch_mode;

nla_put_failure:
    g_return_val_if_reached(-1);
}

/*
 * nm_devlink_set_eswitch_mode:
 * @self: the #NMDevlink
 * @mode: the eswitch mode to set
 * @error: the error location
 *
 * Set the eswitch mode of the device related to the #NMDevlink instance. Note
 * that this might be unsupported by the device (see nm_devlink_get_dev()).
 *
 * Returns: FALSE in case of error, TRUE otherwise
 */
gboolean
nm_devlink_set_eswitch_mode(NMDevlink *self, enum devlink_eswitch_mode mode, GError **error)
{
    nm_auto_nlmsg struct nl_msg *msg     = NULL;
    gs_free char                *bus     = NULL;
    gs_free char                *devname = NULL;

    if (!nm_devlink_get_dev(self, &bus, &devname, error))
        return FALSE;

    msg = devlink_alloc_msg(self, DEVLINK_CMD_ESWITCH_SET, 0);
    NLA_PUT_STRING(msg, DEVLINK_ATTR_BUS_NAME, bus);
    NLA_PUT_STRING(msg, DEVLINK_ATTR_DEV_NAME, devname);
    NLA_PUT_U16(msg, DEVLINK_ATTR_ESWITCH_MODE, mode);

    if (devlink_send_and_recv(self, msg, NULL, NULL) < 0) {
        g_set_error_literal(error,
                            NM_UTILS_ERROR,
                            NM_UTILS_ERROR_UNKNOWN,
                            "devlink: set-eswitch-mode: failed");
        return FALSE;
    }

    _LOGD("set-eswitch-mode: success");

    return TRUE;

nla_put_failure:
    g_return_val_if_reached(FALSE);
}
