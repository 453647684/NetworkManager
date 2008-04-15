/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <glib-object.h>
#include <dbus/dbus-glib.h>
#include <string.h>
#include "nm-connection.h"
#include "nm-utils.h"

#include "nm-setting-8021x.h"
#include "nm-setting-connection.h"
#include "nm-setting-ip4-config.h"
#include "nm-setting-ppp.h"
#include "nm-setting-pppoe.h"
#include "nm-setting-wired.h"
#include "nm-setting-wireless.h"
#include "nm-setting-wireless-security.h"
#include "nm-setting-vpn.h"
#include "nm-setting-vpn-properties.h"

#include "nm-setting-serial.h"
#include "nm-setting-gsm.h"
#include "nm-setting-cdma.h"

typedef struct {
	GHashTable *settings;

	/* Type of the connection (system or user) */
	NMConnectionScope scope;

	/* D-Bus path of the connection, if any */
	char *path;
} NMConnectionPrivate;

#define NM_CONNECTION_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_CONNECTION, NMConnectionPrivate))

G_DEFINE_TYPE (NMConnection, nm_connection, G_TYPE_OBJECT)

enum {
	PROP_0,
	PROP_SCOPE,
	PROP_PATH,

	LAST_PROP
};

enum {
	SECRETS_UPDATED,

	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static GHashTable *registered_settings = NULL;

#define DEFAULT_MAP_SIZE 13

static struct SettingInfo {
	const char *name;
	GType type;
	guint32 priority;
} default_map[DEFAULT_MAP_SIZE] = { { NULL } };

static void
register_one_setting (int i, const char *name, GType type, guint32 priority)
{
	g_return_if_fail (i >= 0);
	g_return_if_fail (i < DEFAULT_MAP_SIZE);
	g_return_if_fail (default_map[i].name == NULL);

	default_map[i].name = name;
	default_map[i].type = type;
	default_map[i].priority = priority;
	nm_setting_register (name, type);
}

static void
register_default_settings (void)
{
	int i = 0;

	nm_utils_register_value_transformations ();

	if (G_LIKELY (default_map[0].name))
		return;

	register_one_setting (i++, NM_SETTING_CONNECTION_SETTING_NAME,        NM_TYPE_SETTING_CONNECTION,        0);
	register_one_setting (i++, NM_SETTING_WIRED_SETTING_NAME,             NM_TYPE_SETTING_WIRED,             1);
	register_one_setting (i++, NM_SETTING_WIRELESS_SETTING_NAME,          NM_TYPE_SETTING_WIRELESS,          1);
	register_one_setting (i++, NM_SETTING_GSM_SETTING_NAME,               NM_TYPE_SETTING_GSM,               1);
	register_one_setting (i++, NM_SETTING_CDMA_SETTING_NAME,              NM_TYPE_SETTING_CDMA,              1);
	register_one_setting (i++, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, NM_TYPE_SETTING_WIRELESS_SECURITY, 2);
	register_one_setting (i++, NM_SETTING_SERIAL_SETTING_NAME,            NM_TYPE_SETTING_SERIAL,            2);
	register_one_setting (i++, NM_SETTING_PPP_SETTING_NAME,               NM_TYPE_SETTING_PPP,               3);
	register_one_setting (i++, NM_SETTING_PPPOE_SETTING_NAME,             NM_TYPE_SETTING_PPPOE,             3);
	register_one_setting (i++, NM_SETTING_802_1X_SETTING_NAME,            NM_TYPE_SETTING_802_1X,            3);
	register_one_setting (i++, NM_SETTING_VPN_SETTING_NAME,               NM_TYPE_SETTING_VPN,               4);
	register_one_setting (i++, NM_SETTING_VPN_PROPERTIES_SETTING_NAME,    NM_TYPE_SETTING_VPN_PROPERTIES,    5);
	register_one_setting (i++, NM_SETTING_IP4_CONFIG_SETTING_NAME,        NM_TYPE_SETTING_IP4_CONFIG,        6);
}

static guint32
get_priority_for_setting_type (GType type)
{
	int i;

	for (i = 0; default_map[i].name; i++) {
		if (default_map[i].type == type)
			return default_map[i].priority;
	}

	return G_MAXUINT32;
}

void
nm_setting_register (const char *name, GType type)
{
	g_return_if_fail (name != NULL);
	g_return_if_fail (G_TYPE_IS_INSTANTIATABLE (type));

	if (!registered_settings)
		registered_settings = g_hash_table_new_full (g_str_hash, g_str_equal, 
											(GDestroyNotify) g_free,
											(GDestroyNotify) g_free);

	if (g_hash_table_lookup (registered_settings, name))
		g_warning ("Already have a creator function for '%s', overriding", name);

	g_hash_table_insert (registered_settings, g_strdup (name), g_strdup (g_type_name (type)));
}

void
nm_setting_unregister (const char *name)
{
	if (registered_settings)
		g_hash_table_remove (registered_settings, name);
}

GType
nm_connection_lookup_setting_type (const char *name)
{
	char *type_name;
	GType type;

	type_name = (char *) g_hash_table_lookup (registered_settings, name);
	if (type_name) {
		type = g_type_from_name (type_name);
		if (!type)
			g_warning ("Can not get type for '%s'.", type_name);
	} else {
		type = 0;
		g_warning ("Unknown setting '%s'", name);
	}

	return type;
}

NMSetting *
nm_connection_create_setting (const char *name)
{
	GType type;
	NMSetting *setting = NULL;

	g_return_val_if_fail (name != NULL, NULL);

	type = nm_connection_lookup_setting_type (name);
	if (type)
		setting = (NMSetting *) g_object_new (type, NULL);

	return setting;
}

static void
parse_one_setting (gpointer key, gpointer value, gpointer user_data)
{
	NMConnection *connection = (NMConnection *) user_data;
	GType type;
	NMSetting *setting = NULL;

	type = nm_connection_lookup_setting_type ((char *) key);
	if (type)
		setting = nm_setting_from_hash (type, (GHashTable *) value);
	if (setting)
		nm_connection_add_setting (connection, setting);
}

void
nm_connection_add_setting (NMConnection *connection, NMSetting *setting)
{
	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (NM_IS_SETTING (setting));

	g_hash_table_insert (NM_CONNECTION_GET_PRIVATE (connection)->settings,
					 g_strdup (G_OBJECT_TYPE_NAME (setting)), setting);
}

void
nm_connection_remove_setting (NMConnection *connection, GType type)
{
	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (g_type_is_a (type, NM_TYPE_SETTING));

	g_hash_table_remove (NM_CONNECTION_GET_PRIVATE (connection)->settings, g_type_name (type));
}

NMSetting *
nm_connection_get_setting (NMConnection *connection, GType type)
{
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);
	g_return_val_if_fail (g_type_is_a (type, NM_TYPE_SETTING), NULL);

	return (NMSetting *) g_hash_table_lookup (NM_CONNECTION_GET_PRIVATE (connection)->settings,
									  g_type_name (type));
}

NMSetting *
nm_connection_get_setting_by_name (NMConnection *connection, const char *name)
{
	GType type;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	type = nm_connection_lookup_setting_type (name);

	return type ? nm_connection_get_setting (connection, type) : NULL;
}

gboolean
nm_connection_replace_settings (NMConnection *connection,
                                GHashTable *new_settings)
{
	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);
	g_return_val_if_fail (new_settings != NULL, FALSE);

	g_hash_table_remove_all (NM_CONNECTION_GET_PRIVATE (connection)->settings);
	g_hash_table_foreach (new_settings, parse_one_setting, connection);

	if (!nm_connection_verify (connection)) {
		g_warning ("Settings invalid.");
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	NMConnection *other;
	gboolean failed;
	NMSettingCompareFlags flags;
} CompareConnectionInfo;

static void
compare_one_setting (gpointer key, gpointer value, gpointer user_data)
{
	NMSetting *setting = (NMSetting *) value;
	CompareConnectionInfo *info = (CompareConnectionInfo *) user_data;
	NMSetting *other_setting;

	if (info->failed)
		return;

	other_setting = nm_connection_get_setting (info->other, G_OBJECT_TYPE (setting));
	if (other_setting)
		info->failed = nm_setting_compare (setting, other_setting, info->flags) ? FALSE : TRUE;
	else
		info->failed = TRUE;
}

gboolean
nm_connection_compare (NMConnection *connection,
                       NMConnection *other,
                       NMSettingCompareFlags flags)
{
	NMConnectionPrivate *priv;
	CompareConnectionInfo info = { other, FALSE, flags };

	if (!connection && !other)
		return TRUE;

	if (!connection || !other)
		return FALSE;

	priv = NM_CONNECTION_GET_PRIVATE (connection);
	g_hash_table_foreach (priv->settings, compare_one_setting, &info);
	if (info.failed == FALSE) {
		/* compare A to B, then if that is the same compare B to A to ensure
		 * that keys that are in B but not A will make the comparison fail.
		 */
		info.failed = FALSE;
		info.other = connection;
		priv = NM_CONNECTION_GET_PRIVATE (other);
		g_hash_table_foreach (priv->settings, compare_one_setting, &info);
	}

	return info.failed ? FALSE : TRUE;
}

typedef struct {
	gboolean success;
	GSList *all_settings;
} VerifySettingsInfo;

static void
verify_one_setting (gpointer data, gpointer user_data)
{
	NMSetting *setting = NM_SETTING (data);
	VerifySettingsInfo *info = (VerifySettingsInfo *) user_data;

	if (info->success)
		info->success = nm_setting_verify (setting, info->all_settings);
}

static void
hash_values_to_slist (gpointer key, gpointer value, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_prepend (*list, value);
}

gboolean
nm_connection_verify (NMConnection *connection)
{
	NMConnectionPrivate *priv;
	NMSetting *connection_setting;
	VerifySettingsInfo info;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), FALSE);

	priv = NM_CONNECTION_GET_PRIVATE (connection);

	/* First, make sure there's at least 'connection' setting */
	connection_setting = nm_connection_get_setting (connection, NM_TYPE_SETTING_CONNECTION);
	if (!connection_setting) {
		g_warning ("'connection' setting not present.");
		return FALSE;
	}

	/* Now, run the verify function of each setting */
	info.success = TRUE;
	info.all_settings = NULL;
	g_hash_table_foreach (priv->settings, hash_values_to_slist, &info.all_settings);

	g_slist_foreach (info.all_settings, verify_one_setting, &info);
	g_slist_free (info.all_settings);

	return info.success;
}

void
nm_connection_update_secrets (NMConnection *connection,
                              const char *setting_name,
                              GHashTable *secrets)
{
	NMSetting *setting;

	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (setting_name != NULL);
	g_return_if_fail (secrets != NULL);

	setting = nm_connection_get_setting (connection, nm_connection_lookup_setting_type (setting_name));
	if (!setting) {
		g_warning ("Unhandled settings object for secrets update.");
		return;
	}

	nm_setting_update_secrets (setting, secrets);
	g_signal_emit (connection, signals[SECRETS_UPDATED], 0, setting_name);
}

static gint
setting_priority_compare (gconstpointer a, gconstpointer b)
{
	guint32 prio_a, prio_b;

	prio_a = get_priority_for_setting_type (G_OBJECT_TYPE (NM_SETTING (a)));
	prio_b = get_priority_for_setting_type (G_OBJECT_TYPE (NM_SETTING (b)));

	if (prio_a < prio_b)
		return -1;
	else if (prio_a == prio_b)
		return 0;
	return 1;
}

static void
add_setting_to_list (gpointer key, gpointer data, gpointer user_data)
{
	GSList **list = (GSList **) user_data;

	*list = g_slist_insert_sorted (*list, NM_SETTING (data), setting_priority_compare);
}

const char *
nm_connection_need_secrets (NMConnection *connection,
                            GPtrArray **hints)
{
	NMConnectionPrivate *priv;
	GSList *settings = NULL;
	GSList *iter;
	char *name = NULL;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	priv = NM_CONNECTION_GET_PRIVATE (connection);

	/* Get list of settings in priority order */
	g_hash_table_foreach (priv->settings, add_setting_to_list, &settings);

	for (iter = settings; iter; iter = g_slist_next (iter)) {
		NMSetting *setting = NM_SETTING (iter->data);
		GPtrArray *secrets;

		// FIXME: do something with requested secrets rather than asking for
		// all of them.  Maybe make secrets a hash table mapping
		// settings name :: [list of secrets key names].
		secrets = nm_setting_need_secrets (setting);
		if (secrets) {
			if (hints)
				*hints = secrets;
			else
				g_ptr_array_free (secrets, TRUE);

			name = (char *) nm_setting_get_name (setting);
			break;
		}
	}

	g_slist_free (settings);
	return name;
}

static void
clear_setting_secrets (gpointer key, gpointer data, gpointer user_data)
{
	nm_setting_clear_secrets (NM_SETTING (data));
}

void
nm_connection_clear_secrets (NMConnection *connection)
{
	NMConnectionPrivate *priv;

	g_return_if_fail (NM_IS_CONNECTION (connection));

	priv = NM_CONNECTION_GET_PRIVATE (connection);
	g_hash_table_foreach (priv->settings, clear_setting_secrets, NULL);
}

static void
add_one_setting_to_hash (gpointer key, gpointer data, gpointer user_data)
{
	NMSetting *setting = (NMSetting *) data;
	GHashTable *connection_hash = (GHashTable *) user_data;
	GHashTable *setting_hash;

	g_return_if_fail (setting != NULL);
	g_return_if_fail (connection_hash != NULL);

	setting_hash = nm_setting_to_hash (setting);
	if (setting_hash)
		g_hash_table_insert (connection_hash,
						 g_strdup (setting->name),
						 setting_hash);
}

GHashTable *
nm_connection_to_hash (NMConnection *connection)
{
	NMConnectionPrivate *priv;
	GHashTable *connection_hash;

	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	connection_hash = g_hash_table_new_full (g_str_hash, g_str_equal,
									 g_free, (GDestroyNotify) g_hash_table_destroy);

	priv = NM_CONNECTION_GET_PRIVATE (connection);
	g_hash_table_foreach (priv->settings, add_one_setting_to_hash, connection_hash);

	/* Don't send empty hashes */
	if (g_hash_table_size (connection_hash) < 1) {
		g_hash_table_destroy (connection_hash);
		connection_hash = NULL;
	}

	return connection_hash;
}

typedef struct ForEachValueInfo {
	NMSettingValueIterFn func;
	gpointer user_data;
} ForEachValueInfo;

static void
for_each_setting (gpointer key, gpointer value, gpointer user_data)
{
	ForEachValueInfo *info = (ForEachValueInfo *) user_data;

	nm_setting_enumerate_values (NM_SETTING (value), info->func, info->user_data);
}

void
nm_connection_for_each_setting_value (NMConnection *connection,
                                       NMSettingValueIterFn func,
                                       gpointer user_data)
{
	NMConnectionPrivate *priv;
	ForEachValueInfo *info;

	g_return_if_fail (NM_IS_CONNECTION (connection));
	g_return_if_fail (func != NULL);

	priv = NM_CONNECTION_GET_PRIVATE (connection);

	info = g_slice_new0 (ForEachValueInfo);
	if (!info) {
		g_warning ("Not enough memory to enumerate values.");
		return;
	}
	info->func = func;
	info->user_data = user_data;

	g_hash_table_foreach (priv->settings, for_each_setting, info);

	g_slice_free (ForEachValueInfo, info);
}

static void
dump_setting (gpointer key, gpointer value, gpointer user_data)
{
	char *str;

	str = nm_setting_to_string (NM_SETTING (value));
	g_print ("%s\n", str);
	g_free (str);
}

void
nm_connection_dump (NMConnection *connection)
{
	g_return_if_fail (NM_IS_CONNECTION (connection));

	g_hash_table_foreach (NM_CONNECTION_GET_PRIVATE (connection)->settings, dump_setting, NULL);
}

void
nm_connection_set_scope (NMConnection *connection, NMConnectionScope scope)
{
	g_return_if_fail (NM_IS_CONNECTION (connection));

	NM_CONNECTION_GET_PRIVATE (connection)->scope = scope;
}

NMConnectionScope
nm_connection_get_scope (NMConnection *connection)
{
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NM_CONNECTION_SCOPE_UNKNOWN);

	return NM_CONNECTION_GET_PRIVATE (connection)->scope;
}

void
nm_connection_set_path (NMConnection *connection, const char *path)
{
	NMConnectionPrivate *priv;

	g_return_if_fail (NM_IS_CONNECTION (connection));

	priv = NM_CONNECTION_GET_PRIVATE (connection);

	if (priv->path) {
		g_free (priv->path);
		priv->path = NULL;
	}

	if (path)
		priv->path = g_strdup (path);
}

const char *
nm_connection_get_path (NMConnection *connection)
{
	g_return_val_if_fail (NM_IS_CONNECTION (connection), NULL);

	return NM_CONNECTION_GET_PRIVATE (connection)->path;
}

NMConnection *
nm_connection_new (void)
{
	GObject *object;

	if (!registered_settings)
		register_default_settings ();

	object = g_object_new (NM_TYPE_CONNECTION, NULL);

	return NM_CONNECTION (object);
}

NMConnection *
nm_connection_new_from_hash (GHashTable *hash)
{
	NMConnection *connection;
	NMConnectionPrivate *priv;

	g_return_val_if_fail (hash != NULL, NULL);

	connection = nm_connection_new ();
	g_hash_table_foreach (hash, parse_one_setting, connection);

	priv = NM_CONNECTION_GET_PRIVATE (connection);

	if (!nm_connection_verify (connection)) {
		g_object_unref (connection);
		return NULL;
	}

	return connection;
}

static void
nm_connection_init (NMConnection *connection)
{
	NMConnectionPrivate *priv = NM_CONNECTION_GET_PRIVATE (connection);

	priv->settings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
}

static void
finalize (GObject *object)
{
	NMConnection *connection = NM_CONNECTION (object);
	NMConnectionPrivate *priv = NM_CONNECTION_GET_PRIVATE (connection);

	g_hash_table_destroy (priv->settings);
	priv->settings = NULL;

	g_free (priv->path);
	priv->path = NULL;

	G_OBJECT_CLASS (nm_connection_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMConnection *connection = NM_CONNECTION (object);

	switch (prop_id) {
	case PROP_SCOPE:
		nm_connection_set_scope (connection, g_value_get_uint (value));
		break;
	case PROP_PATH:
		nm_connection_set_path (connection, g_value_get_string (value));
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
	NMConnection *connection = NM_CONNECTION (object);

	switch (prop_id) {
	case PROP_SCOPE:
		g_value_set_uint (value, nm_connection_get_scope (connection));
		break;
	case PROP_PATH:
		g_value_set_string (value, nm_connection_get_path (connection));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_connection_class_init (NMConnectionClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (NMConnectionPrivate));

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_SCOPE,
		 g_param_spec_uint (NM_CONNECTION_SCOPE,
						    "Scope",
						    "Scope",
						    NM_CONNECTION_SCOPE_UNKNOWN,
						    NM_CONNECTION_SCOPE_USER,
						    NM_CONNECTION_SCOPE_UNKNOWN,
						    G_PARAM_READWRITE));

	g_object_class_install_property
		(object_class, PROP_PATH,
		 g_param_spec_string (NM_CONNECTION_PATH,
						  "Path",
						  "Path",
						  NULL,
						  G_PARAM_READWRITE));

	/* Signals */
	signals[SECRETS_UPDATED] =
		g_signal_new ("secrets-updated",
					  G_OBJECT_CLASS_TYPE (object_class),
					  G_SIGNAL_RUN_FIRST,
					  G_STRUCT_OFFSET (NMConnectionClass, secrets_updated),
					  NULL, NULL,
					  g_cclosure_marshal_VOID__STRING,
					  G_TYPE_NONE, 1,
					  G_TYPE_STRING);
}

