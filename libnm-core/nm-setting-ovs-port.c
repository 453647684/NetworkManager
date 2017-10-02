/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 * Copyright 2017 Red Hat, Inc.
 */

#include "nm-default.h"

#include "nm-setting-ovs-port.h"

#include "nm-connection-private.h"
#include "nm-setting-connection.h"
#include "nm-setting-private.h"

/**
 * SECTION:nm-setting-ovs-port
 * @short_description: Describes connection properties for OpenVSwitch ports.
 *
 * The #NMSettingOvsPort object is a #NMSetting subclass that describes properties
 * necessary for OpenVSwitch ports.
 **/

/**
 * NMSettingOvsPort:
 *
 * OvsPort Link Settings
 */
struct _NMSettingOvsPort {
	NMSetting parent;

	char *vlan_mode;
	guint tag;
	char *lacp;
	char *bond_mode;
	guint bond_updelay;
	guint bond_downdelay;
};

struct _NMSettingOvsPortClass {
	NMSettingClass parent;
};

G_DEFINE_TYPE_WITH_CODE (NMSettingOvsPort, nm_setting_ovs_port, NM_TYPE_SETTING,
                         _nm_register_setting (OVS_PORT, NM_SETTING_PRIORITY_HW_BASE))
NM_SETTING_REGISTER_TYPE (NM_TYPE_SETTING_OVS_PORT)

enum {
	PROP_0,
	PROP_VLAN_MODE,
	PROP_TAG,
	PROP_LACP,
	PROP_BOND_MODE,
	PROP_BOND_UPDELAY,
	PROP_BOND_DOWNDELAY,
	LAST_PROP
};

/**
 * nm_setting_ovs_port_new:
 *
 * Creates a new #NMSettingOvsPort object with default values.
 *
 * Returns: (transfer full): the new empty #NMSettingOvsPort object
 *
 * Since: 1.10
 **/
NMSetting *
nm_setting_ovs_port_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_OVS_PORT, NULL);
}

/**
 * nm_setting_ovs_port_get_vlan_mode:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:vlan-mode property of the setting
 *
 * Since: 1.10
 **/
const char *
nm_setting_ovs_port_get_vlan_mode (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), NULL);

	return s_ovs_port->vlan_mode;
}

/**
 * nm_setting_ovs_port_get_tag:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:tag property of the setting
 *
 * Since: 1.10
 **/
guint
nm_setting_ovs_port_get_tag (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), 0);

	return s_ovs_port->tag;
}

/**
 * nm_setting_ovs_port_get_lacp:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:lacp property of the setting
 *
 * Since: 1.10
 **/
const char *
nm_setting_ovs_port_get_lacp (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), NULL);

	return s_ovs_port->lacp;
}

/**
 * nm_setting_ovs_port_get_bond_mode:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:bond-mode property of the setting
 *
 * Since: 1.10
 **/
const char *
nm_setting_ovs_port_get_bond_mode (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), NULL);

	return s_ovs_port->bond_mode;
}

/**
 * nm_setting_ovs_port_get_bond_updelay:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:bond-updelay property of the setting
 *
 * Since: 1.10
 **/
guint
nm_setting_ovs_port_get_bond_updelay (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), 0);

	return s_ovs_port->bond_updelay;
}

/**
 * nm_setting_ovs_port_get_bond_downdelay:
 * @s_ovs_port: the #NMSettingOvsPort
 *
 * Returns: the #NMSettingOvsPort:bond-downdelay property of the setting
 *
 * Since: 1.10
 **/
guint
nm_setting_ovs_port_get_bond_downdelay (NMSettingOvsPort *s_ovs_port)
{
	g_return_val_if_fail (NM_IS_SETTING_OVS_PORT (s_ovs_port), 0);

	return s_ovs_port->bond_downdelay;
}

static void
set_property (GObject *object, guint prop_id,
              const GValue *value, GParamSpec *pspec)
{
	NMSettingOvsPort *s_ovs_port = NM_SETTING_OVS_PORT (object);

	switch (prop_id) {
	case PROP_VLAN_MODE:
		g_free (s_ovs_port->vlan_mode);
		s_ovs_port->vlan_mode = g_value_dup_string (value);
		break;
	case PROP_TAG:
		s_ovs_port->tag = g_value_get_uint (value);
		break;
	case PROP_LACP:
		g_free (s_ovs_port->lacp);
		s_ovs_port->lacp = g_value_dup_string (value);
		break;
	case PROP_BOND_MODE:
		g_free (s_ovs_port->bond_mode);
		s_ovs_port->bond_mode = g_value_dup_string (value);
		break;
	case PROP_BOND_UPDELAY:
		s_ovs_port->bond_updelay = g_value_get_uint (value);
		break;
	case PROP_BOND_DOWNDELAY:
		s_ovs_port->bond_downdelay = g_value_get_uint (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
get_property (GObject *object, guint prop_id,
              GValue *value, GParamSpec *pspec)
{
	NMSettingOvsPort *s_ovs_port = NM_SETTING_OVS_PORT (object);

	switch (prop_id) {
	case PROP_VLAN_MODE:
		g_value_set_string (value, s_ovs_port->vlan_mode);
		break;
	case PROP_TAG:
		g_value_set_uint (value, s_ovs_port->tag);
		break;
	case PROP_LACP:
		g_value_set_string (value, s_ovs_port->lacp);
		break;
	case PROP_BOND_MODE:
		g_value_set_string (value, s_ovs_port->bond_mode);
		break;
	case PROP_BOND_UPDELAY:
		g_value_set_uint (value, s_ovs_port->bond_updelay);
		break;
	case PROP_BOND_DOWNDELAY:
		g_value_set_uint (value, s_ovs_port->bond_downdelay);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static int
verify (NMSetting *setting, NMConnection *connection, GError **error)
{
	NMSettingOvsPort *s_ovs_port = NM_SETTING_OVS_PORT (setting);

	if (!_nm_connection_verify_required_interface_name (connection, error))
		return FALSE;

	if (!NM_IN_STRSET (s_ovs_port->vlan_mode, "access", "native-tagged", "native-untagged", "trunk", NULL)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not allowed in vlan_mode"),
		             s_ovs_port->vlan_mode);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_OVS_PORT_SETTING_NAME, NM_SETTING_OVS_PORT_VLAN_MODE);
		return FALSE;
	}

	if (s_ovs_port->tag >= 4095) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("the tag id must be in range 0-4094 but is %u"),
		             s_ovs_port->tag);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_OVS_PORT_SETTING_NAME, NM_SETTING_OVS_PORT_TAG);
		return FALSE;
	}

	if (!NM_IN_STRSET (s_ovs_port->lacp, "active", "off", "passive", NULL)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not allowed in lacp"),
		             s_ovs_port->lacp);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_OVS_PORT_SETTING_NAME, NM_SETTING_OVS_PORT_LACP);
		return FALSE;
	}

	if (!NM_IN_STRSET (s_ovs_port->bond_mode, "active-backup", "balance-slb", "balance-tcp", NULL)) {
		g_set_error (error,
		             NM_CONNECTION_ERROR,
		             NM_CONNECTION_ERROR_INVALID_PROPERTY,
		             _("'%s' is not allowed in bond_mode"),
		             s_ovs_port->bond_mode);
		g_prefix_error (error, "%s.%s: ", NM_SETTING_OVS_PORT_SETTING_NAME, NM_SETTING_OVS_PORT_BOND_MODE);
		return FALSE;
	}

	return TRUE;
}

static void
finalize (GObject *object)
{
	NMSettingOvsPort *s_ovs_port = NM_SETTING_OVS_PORT (object);

	g_free (s_ovs_port->vlan_mode);
	g_free (s_ovs_port->lacp);
	g_free (s_ovs_port->bond_mode);

	G_OBJECT_CLASS (nm_setting_ovs_port_parent_class)->finalize (object);
}

static void
nm_setting_ovs_port_init (NMSettingOvsPort *s_ovs_port)
{
}

static void
nm_setting_ovs_port_class_init (NMSettingOvsPortClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;
	parent_class->verify = verify;

	/**
	 * NMSettingOvsPort:vlan_mode:
	 *
	 * The VLAN mode. One of "access", "native-tagged", "native-untagged",
	 * "trunk" or unset.
	 **/
	g_object_class_install_property
		(object_class, PROP_VLAN_MODE,
		 g_param_spec_string (NM_SETTING_OVS_PORT_VLAN_MODE, "", "",
	                              NULL,
	                              G_PARAM_READWRITE |
	                              G_PARAM_CONSTRUCT |
	                              NM_SETTING_PARAM_INFERRABLE |
	                              G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsPort:vlan_tag:
	 *
	 * The VLAN tag in the range 0-4095.
	 **/
	g_object_class_install_property
		(object_class, PROP_TAG,
		 g_param_spec_uint (NM_SETTING_OVS_PORT_TAG, "", "",
	                            0, 4095, 0,
	                            G_PARAM_READWRITE |
	                            G_PARAM_CONSTRUCT |
	                            NM_SETTING_PARAM_INFERRABLE |
	                            G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsPort:lacp:
	 *
	 * LACP mode. One of "active", "off", or "passive".
	 **/
	g_object_class_install_property
		(object_class, PROP_LACP,
		 g_param_spec_string (NM_SETTING_OVS_PORT_LACP, "", "",
	                              NULL,
	                              G_PARAM_READWRITE |
	                              G_PARAM_CONSTRUCT |
	                              NM_SETTING_PARAM_INFERRABLE |
	                              G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsPort:bond-mode:
	 *
	 * Bonding mode. One of "active-backup", "balance-slb", or "balance-tcp".
	 **/
	g_object_class_install_property
		(object_class, PROP_BOND_MODE,
		 g_param_spec_string (NM_SETTING_OVS_PORT_BOND_MODE, "", "",
	                              NULL,
	                              G_PARAM_READWRITE |
	                              G_PARAM_CONSTRUCT |
	                              NM_SETTING_PARAM_INFERRABLE |
	                              G_PARAM_STATIC_STRINGS));


	/**
	 * NMSettingOvsPort:updelay:
	 *
	 * The time port must be active befor it starts forwarding traffic.
	 **/
	g_object_class_install_property
		(object_class, PROP_BOND_UPDELAY,
		 g_param_spec_uint (NM_SETTING_OVS_PORT_BOND_UPDELAY, "", "",
	                            0, G_MAXUINT, 0,
	                            G_PARAM_READWRITE |
	                            G_PARAM_CONSTRUCT |
	                            NM_SETTING_PARAM_INFERRABLE |
	                            G_PARAM_STATIC_STRINGS));

	/**
	 * NMSettingOvsPort:downdelay:
	 *
	 * The time port must be inactive in order to be considered down.
	 **/
	g_object_class_install_property
		(object_class, PROP_BOND_DOWNDELAY,
		 g_param_spec_uint (NM_SETTING_OVS_PORT_BOND_DOWNDELAY, "", "",
	                            0, G_MAXUINT, 0,
	                            G_PARAM_READWRITE |
	                            G_PARAM_CONSTRUCT |
	                            NM_SETTING_PARAM_INFERRABLE |
	                            G_PARAM_STATIC_STRINGS));
}
