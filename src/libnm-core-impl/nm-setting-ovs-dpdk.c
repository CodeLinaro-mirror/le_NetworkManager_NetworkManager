/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Copyright (C) 2019 Red Hat, Inc.
 */

#include "libnm-core-impl/nm-default-libnm-core.h"

#include "nm-setting-ovs-dpdk.h"

#include "nm-connection-private.h"
#include "nm-setting-connection.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-ovs-dpdk
 * @short_description: Describes connection properties for Open vSwitch DPDK interfaces.
 *
 * The #NMSettingOvsDpdk object is a #NMSetting subclass that describes properties
 * necessary for Open vSwitch interfaces of type "dpdk".
 **/

/*****************************************************************************/

NM_GOBJECT_PROPERTIES_DEFINE_BASE(PROP_DEVARGS, PROP_N_RXQ, );

// PS.: Review changes suggested by Beniamino and Thomas are in a new commit
// that will just be included here too, this is just for a test

/**
 * NMSettingOvsDpdk:
 *
 * OvsDpdk Link Settings
 */
struct _NMSettingOvsDpdk {
    NMSetting parent;

    char *devargs;
    char *n_rxq;
};

struct _NMSettingOvsDpdkClass {
    NMSettingClass parent;
};

G_DEFINE_TYPE(NMSettingOvsDpdk, nm_setting_ovs_dpdk, NM_TYPE_SETTING)

/*****************************************************************************/

/**
 * nm_setting_ovs_dpdk_get_devargs:
 * @self: the #NMSettingOvsDpdk
 *
 * Returns: the #NMSettingOvsDpdk:devargs property of the setting
 *
 * Since: 1.20
 **/
const char *
nm_setting_ovs_dpdk_get_devargs(NMSettingOvsDpdk *self)
{
    g_return_val_if_fail(NM_IS_SETTING_OVS_DPDK(self), NULL);

    return self->devargs;
}


/**
 * nm_setting_ovs_dpdk_get_n_rxq:
 * @self: the #NMSettingOvsDpdk
 *
 * Returns: the #NMSettingOvsDpdk:n_rxq property of the setting
 *
 * Since: 1.35
 **/
const char *
nm_setting_ovs_dpdk_get_n_rxq(NMSettingOvsDpdk *self)
{
    g_return_val_if_fail(NM_IS_SETTING_OVS_DPDK(self), NULL);

    return self->n_rxq;
}


/*****************************************************************************/

static void
get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
    NMSettingOvsDpdk *self = NM_SETTING_OVS_DPDK(object);

    switch (prop_id) {
    case PROP_DEVARGS:
        g_value_set_string(value, self->devargs);
        break;
    case PROP_N_RXQ:
    	g_value_set_string(value, self->n_rxq);
    	break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
    NMSettingOvsDpdk *self = NM_SETTING_OVS_DPDK(object);
    gint int_val;

    switch (prop_id) {
    case PROP_DEVARGS:
        g_free(self->devargs);
        self->devargs = g_value_dup_string(value);
        break;
    case PROP_N_RXQ:
    	g_free(self->n_rxq);
    	int_val = _nm_utils_ascii_str_to_int64(g_value_get_string(value), 10, 1, G_MAXINT32, 0);
    	if(int_val)
    		self->n_rxq = g_value_dup_string(value);
    	break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/*****************************************************************************/

static void
nm_setting_ovs_dpdk_init(NMSettingOvsDpdk *self)
{}

/**
 * nm_setting_ovs_dpdk_new:
 *
 * Creates a new #NMSettingOvsDpdk object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingOvsDpdk object
 *
 * Since: 1.20
 **/
NMSetting *
nm_setting_ovs_dpdk_new(void)
{
    return g_object_new(NM_TYPE_SETTING_OVS_DPDK, NULL);
}

static void
finalize(GObject *object)
{
    NMSettingOvsDpdk *self = NM_SETTING_OVS_DPDK(object);

    g_free(self->devargs);
    g_free(self->n_rxq);

    G_OBJECT_CLASS(nm_setting_ovs_dpdk_parent_class)->finalize(object);
}

static void
nm_setting_ovs_dpdk_class_init(NMSettingOvsDpdkClass *klass)
{
    GObjectClass   *object_class  = G_OBJECT_CLASS(klass);
    NMSettingClass *setting_class = NM_SETTING_CLASS(klass);

    object_class->set_property = set_property;
    object_class->get_property = get_property;
    object_class->finalize     = finalize;

    /**
     * NMSettingOvsDpdk:devargs:
     *
     * Open vSwitch DPDK device arguments.
     *
     * Since: 1.20
     **/
    obj_properties[PROP_DEVARGS] = g_param_spec_string(
        NM_SETTING_OVS_DPDK_DEVARGS,
        "",
        "",
        NULL,
        G_PARAM_READWRITE | NM_SETTING_PARAM_INFERRABLE | G_PARAM_STATIC_STRINGS);

    /**
	 * NMSettingOvsDpdk:n_rxq:
	 *
	 * Open vSwitch DPDK device arguments.
	 *
	 * Since: 1.35
	 **/
	obj_properties[PROP_N_RXQ] = g_param_spec_string(
		NM_SETTING_OVS_DPDK_N_RXQ,
		"",
		"",
		NULL,
		G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(object_class, _PROPERTY_ENUMS_LAST, obj_properties);

    _nm_setting_class_commit(setting_class, NM_META_SETTING_TYPE_OVS_DPDK, NULL, NULL, 0);
}
