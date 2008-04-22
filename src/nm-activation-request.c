/* NetworkManager -- Network link manager
 *
 * Dan Williams <dcbw@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * (C) Copyright 2005 Red Hat, Inc.
 */

#include <string.h>
#include <dbus/dbus-glib.h>

#include "nm-activation-request.h"
#include "nm-marshal.h"
#include "nm-utils.h"
#include "nm-setting-wireless-security.h"
#include "nm-setting-8021x.h"
#include "nm-dbus-manager.h"
#include "nm-device.h"
#include "nm-properties-changed-signal.h"
#include "nm-active-connection.h"
#include "nm-dbus-glib-types.h"

#include "nm-manager.h" /* FIXME! */

#define CONNECTION_GET_SECRETS_CALL_TAG "get-secrets-call"

G_DEFINE_TYPE (NMActRequest, nm_act_request, G_TYPE_OBJECT)

#define NM_ACT_REQUEST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_ACT_REQUEST, NMActRequestPrivate))

enum {
	CONNECTION_SECRETS_UPDATED,
	CONNECTION_SECRETS_FAILED,
	PROPERTIES_CHANGED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };


typedef struct {
	NMConnection *connection;
	char *specific_object;
	NMConnection *shared;
	NMDevice *device;
	gboolean user_requested;

	NMActiveConnectionState state;
	gboolean is_default;

	char *ac_path;
} NMActRequestPrivate;

enum {
	PROP_0,
	PROP_SERVICE_NAME,
	PROP_CONNECTION,
	PROP_SPECIFIC_OBJECT,
	PROP_SHARED_SERVICE_NAME,
	PROP_SHARED_CONNECTION,
	PROP_DEVICES,
	PROP_STATE,
	PROP_DEFAULT,
	PROP_VPN,

	LAST_PROP
};

static void device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data);


NMActRequest *
nm_act_request_new (NMConnection *connection,
                    const char *specific_object,
                    gboolean user_requested,
                    gpointer *device)
{
	GObject *object;
	NMActRequestPrivate *priv;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);
	g_return_val_if_fail (NM_DEVICE (device), NULL);

	object = g_object_new (NM_TYPE_ACT_REQUEST, NULL);
	if (!object)
		return NULL;

	priv = NM_ACT_REQUEST_GET_PRIVATE (object);

	priv->connection = g_object_ref (connection);
	if (specific_object)
		priv->specific_object = g_strdup (specific_object);

	priv->device = NM_DEVICE (device);
	g_signal_connect (device, "state-changed",
	                  G_CALLBACK (device_state_changed),
	                  NM_ACT_REQUEST (object));

	priv->user_requested = user_requested;

	return NM_ACT_REQUEST (object);
}

static void
nm_act_request_init (NMActRequest *req)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	NMDBusManager *dbus_mgr;

	priv->ac_path = nm_active_connection_get_next_object_path ();
	priv->state = NM_ACTIVE_CONNECTION_STATE_UNKNOWN;

	dbus_mgr = nm_dbus_manager_get ();
	dbus_g_connection_register_g_object (nm_dbus_manager_get_connection (dbus_mgr),
	                                     priv->ac_path,
	                                     G_OBJECT (req));
	g_object_unref (dbus_mgr);
}

static void
dispose (GObject *object)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);
	DBusGProxy *proxy;
	DBusGProxyCall *call;

	if (!priv->connection)
		goto out;

	g_signal_handlers_disconnect_by_func (G_OBJECT (priv->device),
	                                      G_CALLBACK (device_state_changed),
	                                      NM_ACT_REQUEST (object));

	proxy = g_object_get_data (G_OBJECT (priv->connection),
	                           NM_MANAGER_CONNECTION_SECRETS_PROXY_TAG);
	call = g_object_get_data (G_OBJECT (priv->connection),
	                          CONNECTION_GET_SECRETS_CALL_TAG);

	if (proxy && call)
		dbus_g_proxy_cancel_call (proxy, call);

	g_object_set_data (G_OBJECT (priv->connection),
	                   CONNECTION_GET_SECRETS_CALL_TAG, NULL);
	g_object_unref (priv->connection);

	if (priv->shared)
		g_object_unref (priv->shared);

out:
	G_OBJECT_CLASS (nm_act_request_parent_class)->dispose (object);
}

static void
finalize (GObject *object)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);

	g_free (priv->specific_object);
	g_free (priv->ac_path);

	G_OBJECT_CLASS (nm_act_request_parent_class)->finalize (object);
}

static void
get_property (GObject *object, guint prop_id,
			  GValue *value, GParamSpec *pspec)
{
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (object);
	GPtrArray *devices;

	switch (prop_id) {
	case PROP_SERVICE_NAME:
		nm_active_connection_scope_to_value (priv->connection, value);
		break;
	case PROP_CONNECTION:
		g_value_set_boxed (value, nm_connection_get_path (priv->connection));
		break;
	case PROP_SPECIFIC_OBJECT:
		if (priv->specific_object)
			g_value_set_boxed (value, priv->specific_object);
		else
			g_value_set_boxed (value, "/");
		break;
	case PROP_SHARED_SERVICE_NAME:
		nm_active_connection_scope_to_value (priv->shared, value);
		break;
	case PROP_SHARED_CONNECTION:
		if (priv->shared)
			g_value_set_boxed (value, nm_connection_get_path (priv->shared));
		else
			g_value_set_boxed (value, "/");
		break;
	case PROP_DEVICES:
		devices = g_ptr_array_sized_new (1);
		g_ptr_array_add (devices, g_strdup (nm_device_get_udi (priv->device)));
		g_value_take_boxed (value, devices);
		break;
	case PROP_STATE:
		g_value_set_uint (value, priv->state);
		break;
	case PROP_DEFAULT:
		g_value_set_boolean (value, priv->is_default);
		break;
	case PROP_VPN:
		g_value_set_boolean (value, FALSE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_act_request_class_init (NMActRequestClass *req_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (req_class);

	g_type_class_add_private (req_class, sizeof (NMActRequestPrivate));

	/* virtual methods */
	object_class->get_property = get_property;
	object_class->dispose = dispose;
	object_class->finalize = finalize;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_SERVICE_NAME,
		 g_param_spec_string (NM_ACTIVE_CONNECTION_SERVICE_NAME,
							  "Service name",
							  "Service name",
							  NULL,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_CONNECTION,
		 g_param_spec_boxed (NM_ACTIVE_CONNECTION_CONNECTION,
							  "Connection",
							  "Connection",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_SPECIFIC_OBJECT,
		 g_param_spec_boxed (NM_ACTIVE_CONNECTION_SPECIFIC_OBJECT,
							  "Specific object",
							  "Specific object",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_SHARED_SERVICE_NAME,
		 g_param_spec_string (NM_ACTIVE_CONNECTION_SHARED_SERVICE_NAME,
							  "Shared service name",
							  "Shared service name",
							  NULL,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_SHARED_CONNECTION,
		 g_param_spec_boxed (NM_ACTIVE_CONNECTION_SHARED_CONNECTION,
							  "Shared connection",
							  "Shared connection",
							  DBUS_TYPE_G_OBJECT_PATH,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_DEVICES,
		 g_param_spec_boxed (NM_ACTIVE_CONNECTION_DEVICES,
							  "Devices",
							  "Devices",
							  DBUS_TYPE_G_ARRAY_OF_OBJECT_PATH,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_STATE,
		 g_param_spec_uint (NM_ACTIVE_CONNECTION_STATE,
							  "State",
							  "State",
							  NM_ACTIVE_CONNECTION_STATE_UNKNOWN,
							  NM_ACTIVE_CONNECTION_STATE_ACTIVATED,
							  NM_ACTIVE_CONNECTION_STATE_UNKNOWN,
							  G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_DEFAULT,
		 g_param_spec_boolean (NM_ACTIVE_CONNECTION_DEFAULT,
							   "Default",
							   "Is the default active connection",
							   FALSE,
							   G_PARAM_READABLE));
	g_object_class_install_property
		(object_class, PROP_VPN,
		 g_param_spec_boolean (NM_ACTIVE_CONNECTION_VPN,
							   "VPN",
							   "Is a VPN connection",
							   FALSE,
							   G_PARAM_READABLE));

	/* Signals */
	signals[CONNECTION_SECRETS_UPDATED] =
		g_signal_new ("connection-secrets-updated",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMActRequestClass, connection_secrets_updated),
					  NULL, NULL,
					  nm_marshal_VOID__OBJECT_POINTER,
					  G_TYPE_NONE, 2,
					  G_TYPE_OBJECT, G_TYPE_POINTER);

	signals[CONNECTION_SECRETS_FAILED] =
		g_signal_new ("connection-secrets-failed",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMActRequestClass, connection_secrets_failed),
					  NULL, NULL,
					  nm_marshal_VOID__OBJECT_STRING,
					  G_TYPE_NONE, 2,
					  G_TYPE_OBJECT, G_TYPE_STRING);

	signals[PROPERTIES_CHANGED] = 
		nm_properties_changed_signal_new (object_class,
								    G_STRUCT_OFFSET (NMActRequestClass, properties_changed));

	nm_active_connection_install_type_info (object_class);
}

static void
device_state_changed (NMDevice *device, NMDeviceState state, gpointer user_data)
{
	NMActRequest *self = NM_ACT_REQUEST (user_data);
	NMActRequestPrivate *priv = NM_ACT_REQUEST_GET_PRIVATE (self);
	NMActiveConnectionState new_state;
	gboolean new_default = FALSE;

	/* Set NMActiveConnection state based on the device's state */
	switch (state) {
	case NM_DEVICE_STATE_PREPARE:
	case NM_DEVICE_STATE_CONFIG:
	case NM_DEVICE_STATE_NEED_AUTH:
	case NM_DEVICE_STATE_IP_CONFIG:
		new_state = NM_ACTIVE_CONNECTION_STATE_ACTIVATING;
		break;
	case NM_DEVICE_STATE_ACTIVATED:
		new_state = NM_ACTIVE_CONNECTION_STATE_ACTIVATED;
		new_default = priv->is_default;
		break;
	default:
		new_state = NM_ACTIVE_CONNECTION_STATE_UNKNOWN;
		break;
	}

	if (new_state != priv->state) {
		priv->state = new_state;
		g_object_notify (G_OBJECT (self), NM_ACTIVE_CONNECTION_STATE);
	}

	if (new_default != priv->is_default) {
		priv->is_default = new_default;
		g_object_notify (G_OBJECT (self), NM_ACTIVE_CONNECTION_DEFAULT);
	}
}

typedef struct GetSecretsInfo {
	NMActRequest *req;
	char *setting_name;
} GetSecretsInfo;

static void
free_get_secrets_info (gpointer data)
{
	GetSecretsInfo *info = (GetSecretsInfo *) data;

	g_free (info->setting_name);
	g_free (info);
}

static void
update_one_setting (const char* key,
                    GHashTable *setting_hash,
                    NMConnection *connection,
                    GSList **updated)
{
	GType type;
	NMSetting *setting = NULL;

	/* Check whether a complete & valid NMSetting object was returned.  If
	 * yes, replace the setting object in the connection.  If not, just try
	 * updating the secrets.
	 */
	type = nm_connection_lookup_setting_type (key);
	if (type == 0)
		return;

	setting = nm_setting_from_hash (type, setting_hash);
	if (setting) {
		NMSetting *s_8021x = NULL;
		GSList *all_settings = NULL;

		/* The wireless-security setting might need the 802.1x setting in
		 * the all_settings argument of the verify function. Ugh.
		 */
		s_8021x = nm_connection_get_setting (connection, NM_TYPE_SETTING_802_1X);
		if (s_8021x)
			all_settings = g_slist_append (all_settings, s_8021x);

		if (!nm_setting_verify (setting, all_settings)) {
			/* Just try updating secrets */
			g_object_unref (setting);
			setting = NULL;
		}

		g_slist_free (all_settings);
	}

	if (setting)
		nm_connection_add_setting (connection, setting);
	else
		nm_connection_update_secrets (connection, key, setting_hash);

	*updated = g_slist_append (*updated, (gpointer) key);
}

static void
add_one_key_to_list (gpointer key, gpointer data, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_append (*list, key);
}

static gint
settings_order_func (gconstpointer a, gconstpointer b)
{
	/* Just ensure the 802.1x setting gets processed _before_ the
	 * wireless-security one.
	 */

	if (   !strcmp (a, NM_SETTING_802_1X_SETTING_NAME)
	    && !strcmp (b, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME))
		return -1;

	if (   !strcmp (a, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME)
	    && !strcmp (b, NM_SETTING_802_1X_SETTING_NAME))
		return 1;

	return 0;
}

static void
get_secrets_cb (DBusGProxy *proxy, DBusGProxyCall *call, gpointer user_data)
{
	GetSecretsInfo *info = (GetSecretsInfo *) user_data;
	GError *err = NULL;
	GHashTable *settings = NULL;
	NMActRequestPrivate *priv = NULL;
	GSList *keys = NULL, *iter;
	GSList *updated = NULL;

	g_return_if_fail (info != NULL);
	g_return_if_fail (info->req);
	g_return_if_fail (info->setting_name);

	priv = NM_ACT_REQUEST_GET_PRIVATE (info->req);
	g_object_set_data (G_OBJECT (priv->connection), CONNECTION_GET_SECRETS_CALL_TAG, NULL);

	if (!dbus_g_proxy_end_call (proxy, call, &err,
								DBUS_TYPE_G_MAP_OF_MAP_OF_VARIANT, &settings,
								G_TYPE_INVALID)) {
		nm_warning ("Couldn't get connection secrets: %s.", err->message);
		g_error_free (err);
		g_signal_emit (info->req,
		               signals[CONNECTION_SECRETS_FAILED],
		               0,
		               priv->connection,
		               info->setting_name);
		return;
	}

	if (g_hash_table_size (settings) == 0) {
		// FIXME: some better way to handle invalid message?
		nm_warning ("GetSecrets call returned but no secrets were found.");
		goto out;
	}

	g_hash_table_foreach (settings, add_one_key_to_list, &keys);
	keys = g_slist_sort (keys, settings_order_func);
	for (iter = keys; iter; iter = g_slist_next (iter)) {
		GHashTable *setting_hash;

		setting_hash = g_hash_table_lookup (settings, iter->data);
		if (setting_hash) {
			update_one_setting ((const char *) iter->data,
			                    setting_hash,
			                    priv->connection,
			                    &updated);
		} else
			nm_warning ("Couldn't get setting secrets for '%s'", (const char *) iter->data);
	}
	g_slist_free (keys);

	if (g_slist_length (updated)) {
		g_signal_emit (info->req,
		               signals[CONNECTION_SECRETS_UPDATED],
		               0,
		               priv->connection,
		               updated);
	} else {
		nm_warning ("No secrets updated because not valid settings were received!");
	}

out:
	g_slist_free (updated);
	g_hash_table_destroy (settings);
}

gboolean
nm_act_request_request_connection_secrets (NMActRequest *req,
                                           const char *setting_name,
                                           gboolean request_new)
{
	DBusGProxy *proxy;
	DBusGProxyCall *call;
	GetSecretsInfo *info = NULL;
	NMActRequestPrivate *priv = NULL;
	GPtrArray *hints = NULL;

	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);
	g_return_val_if_fail (setting_name != NULL, FALSE);

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	proxy = g_object_get_data (G_OBJECT (priv->connection), NM_MANAGER_CONNECTION_SECRETS_PROXY_TAG);
	if (!DBUS_IS_G_PROXY (proxy)) {
		nm_warning ("Couldn't get dbus proxy for connection.");
		goto error;
	}

	info = g_malloc0 (sizeof (GetSecretsInfo));
	if (!info) {
		nm_warning ("Not enough memory to get secrets");
		goto error;
	}

	info->setting_name = g_strdup (setting_name);
	if (!info->setting_name) {
		nm_warning ("Not enough memory to get secrets");
		goto error;
	}

	/* Empty for now */
	hints = g_ptr_array_new ();

	info->req = req;
	call = dbus_g_proxy_begin_call_with_timeout (proxy, "GetSecrets",
	                                             get_secrets_cb,
	                                             info,
	                                             free_get_secrets_info,
	                                             G_MAXINT32,
	                                             G_TYPE_STRING, setting_name,
	                                             DBUS_TYPE_G_ARRAY_OF_STRING, hints,
	                                             G_TYPE_BOOLEAN, request_new,
	                                             G_TYPE_INVALID);
	g_ptr_array_free (hints, TRUE);
	if (!call) {
		nm_warning ("Could not call GetSecrets");
		goto error;
	}

	g_object_set_data (G_OBJECT (priv->connection), CONNECTION_GET_SECRETS_CALL_TAG, call);
	return TRUE;

error:
	if (info)
		free_get_secrets_info (info);
	return FALSE;
}

NMConnection *
nm_act_request_get_connection (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->connection;
}

const char *
nm_act_request_get_specific_object (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), NULL);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->specific_object;
}

void
nm_act_request_set_specific_object (NMActRequest *req,
                                    const char *specific_object)
{
	NMActRequestPrivate *priv;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));
	g_return_if_fail (specific_object != NULL);

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);

	if (priv->specific_object)
		g_free (priv->specific_object);
	priv->specific_object = g_strdup (specific_object);
}

gboolean
nm_act_request_get_user_requested (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->user_requested;
}

const char *
nm_act_request_get_active_connection_path (NMActRequest *req)
{
	g_return_val_if_fail (NM_IS_ACT_REQUEST (req), FALSE);

	return NM_ACT_REQUEST_GET_PRIVATE (req)->ac_path;
}

void
nm_act_request_set_default (NMActRequest *req, gboolean is_default)
{
	NMActRequestPrivate *priv;

	g_return_if_fail (NM_IS_ACT_REQUEST (req));

	priv = NM_ACT_REQUEST_GET_PRIVATE (req);
	if (priv->is_default == is_default)
		return;

	priv->is_default = is_default;
	g_object_notify (G_OBJECT (req), NM_ACTIVE_CONNECTION_DEFAULT);
}

