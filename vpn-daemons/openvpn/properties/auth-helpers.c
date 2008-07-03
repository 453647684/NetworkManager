/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/***************************************************************************
 *
 * Copyright (C) 2008 Dan Williams, <dcbw@redhat.com>
 * Copyright (C) 2008 Tambet Ingo, <tambet@gmail.com>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 **************************************************************************/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib/gi18n-lib.h>

#include "auth-helpers.h"
#include "nm-openvpn.h"
#include "../src/nm-openvpn-service.h"

void
tls_pw_init_auth_widget (GladeXML *xml,
                         GtkSizeGroup *group,
                         NMSettingVPNProperties *s_vpn_props,
                         gint contype,
                         const char *prefix,
                         ChangedCallback changed_cb,
                         gpointer user_data)
{
	GtkWidget *widget;
	GValue *value;
	char *tmp;
	GtkFileFilter *filter;

	g_return_if_fail (xml != NULL);
	g_return_if_fail (group != NULL);
	g_return_if_fail (changed_cb != NULL);
	g_return_if_fail (prefix != NULL);

	tmp = g_strdup_printf ("%s_ca_cert_chooser", prefix);
	widget = glade_xml_get_widget (xml, tmp);
	g_free (tmp);

	gtk_size_group_add_widget (group, widget);
	filter = tls_file_chooser_filter_new ();
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
	                                   _("Choose a Certificate Authority certificate..."));
	g_signal_connect (G_OBJECT (widget), "selection-changed", G_CALLBACK (changed_cb), user_data);

	if (s_vpn_props && s_vpn_props->data) {
		value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_CA);
		if (value && G_VALUE_HOLDS_STRING (value))
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), g_value_get_string (value));
	}

	if (contype == NM_OPENVPN_CONTYPE_TLS || contype == NM_OPENVPN_CONTYPE_PASSWORD_TLS) {
		tmp = g_strdup_printf ("%s_user_cert_chooser", prefix);
		widget = glade_xml_get_widget (xml, tmp);
		g_free (tmp);

		gtk_size_group_add_widget (group, widget);
		filter = tls_file_chooser_filter_new ();
		gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
		gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
		gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
		                                   _("Choose your personal certificate..."));
		g_signal_connect (G_OBJECT (widget), "selection-changed", G_CALLBACK (changed_cb), user_data);

		if (s_vpn_props && s_vpn_props->data) {
			value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_CERT);
			if (value && G_VALUE_HOLDS_STRING (value))
				gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), g_value_get_string (value));
		}

		tmp = g_strdup_printf ("%s_private_key_chooser", prefix);
		widget = glade_xml_get_widget (xml, tmp);
		g_free (tmp);

		gtk_size_group_add_widget (group, widget);
		filter = tls_file_chooser_filter_new ();
		gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
		gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
		gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
		                                   _("Choose your private key..."));
		g_signal_connect (G_OBJECT (widget), "selection-changed", G_CALLBACK (changed_cb), user_data);

		if (s_vpn_props && s_vpn_props->data) {
			value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_KEY);
			if (value && G_VALUE_HOLDS_STRING (value))
				gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), g_value_get_string (value));
		}
	}

	if (contype == NM_OPENVPN_CONTYPE_PASSWORD || contype == NM_OPENVPN_CONTYPE_PASSWORD_TLS) {
		tmp = g_strdup_printf ("%s_username_entry", prefix);
		widget = glade_xml_get_widget (xml, tmp);
		g_free (tmp);

		gtk_size_group_add_widget (group, widget);
		if (s_vpn_props && s_vpn_props->data) {
			value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_USERNAME);
			if (value && G_VALUE_HOLDS_STRING (value))
				gtk_entry_set_text (GTK_ENTRY (widget), g_value_get_string (value));
		}
		g_signal_connect (G_OBJECT (widget), "changed", G_CALLBACK (changed_cb), user_data);
	}
}

#define SK_DIR_COL_NAME 0
#define SK_DIR_COL_NUM  1

void
sk_init_auth_widget (GladeXML *xml,
                     GtkSizeGroup *group,
                     NMSettingVPNProperties *s_vpn_props,
                     ChangedCallback changed_cb,
                     gpointer user_data)
{
	GtkWidget *widget;
	GValue *value = NULL;
	GtkListStore *store;
	GtkTreeIter iter;
	gint active = -1;
	gint direction = -1;
	GtkFileFilter *filter;

	g_return_if_fail (xml != NULL);
	g_return_if_fail (group != NULL);
	g_return_if_fail (changed_cb != NULL);

	widget = glade_xml_get_widget (xml, "sk_key_chooser");
	gtk_size_group_add_widget (group, widget);
	filter = sk_file_chooser_filter_new ();
	gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (widget), filter);
	gtk_file_chooser_set_local_only (GTK_FILE_CHOOSER (widget), TRUE);
	gtk_file_chooser_button_set_title (GTK_FILE_CHOOSER_BUTTON (widget),
	                                   _("Choose an OpenVPN static key..."));
	g_signal_connect (G_OBJECT (widget), "selection-changed", G_CALLBACK (changed_cb), user_data);

	if (s_vpn_props && s_vpn_props->data) {
		value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_SHARED_KEY);
		if (value && G_VALUE_HOLDS_STRING (value))
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), g_value_get_string (value));
	}

	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

	if (s_vpn_props && s_vpn_props->data) {
		value = g_hash_table_lookup (s_vpn_props->data, NM_OPENVPN_KEY_SHARED_KEY_DIRECTION);
		if (value && G_VALUE_HOLDS_INT (value))
			direction = g_value_get_int (value);
	}

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, SK_DIR_COL_NAME, _("None"), SK_DIR_COL_NUM, -1, -1);

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, SK_DIR_COL_NAME, "0", SK_DIR_COL_NUM, 0, -1);
	if (value && G_VALUE_HOLDS_INT (value)) {
		if (g_value_get_int (value) == 0)
			active = 1;
	}

	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, SK_DIR_COL_NAME, "1", SK_DIR_COL_NUM, 1, -1);
	if (value && G_VALUE_HOLDS_INT (value)) {
		if (g_value_get_int (value) == 1)
			active = 2;
	}

	widget = glade_xml_get_widget (xml, "sk_direction_combo");
	gtk_size_group_add_widget (group, widget);

	gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
	g_object_unref (store);
	gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active < 0 ? 0 : active);

	widget = glade_xml_get_widget (xml, "sk_dir_help_label");
	gtk_size_group_add_widget (group, widget);
}

static gboolean
validate_file_chooser (GladeXML *xml, const char *name)
{
	GtkWidget *widget;
	char *str;

	widget = glade_xml_get_widget (xml, name);
	str = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!str || !strlen (str))
		return FALSE;
	return TRUE;
}

static gboolean
validate_tls (GladeXML *xml, const char *prefix, GError **error)
{
	char *tmp;
	gboolean valid;

	tmp = g_strdup_printf ("%s_ca_cert_chooser", prefix);
	valid = validate_file_chooser (xml, tmp);
	g_free (tmp);
	if (!valid) {
		g_set_error (error,
		             OPENVPN_PLUGIN_UI_ERROR,
		             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
		             NM_OPENVPN_KEY_CA);
		return FALSE;
	}

	tmp = g_strdup_printf ("%s_user_cert_chooser", prefix);
	valid = validate_file_chooser (xml, tmp);
	g_free (tmp);
	if (!valid) {
		g_set_error (error,
		             OPENVPN_PLUGIN_UI_ERROR,
		             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
		             NM_OPENVPN_KEY_CERT);
		return FALSE;
	}

	tmp = g_strdup_printf ("%s_private_key_chooser", prefix);
	valid = validate_file_chooser (xml, tmp);
	g_free (tmp);
	if (!valid) {
		g_set_error (error,
		             OPENVPN_PLUGIN_UI_ERROR,
		             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
		             NM_OPENVPN_KEY_KEY);
		return FALSE;
	}

	return TRUE;
}

gboolean
auth_widget_check_validity (GladeXML *xml, gint contype, GError **error)
{
	GtkWidget *widget;
	gboolean is_valid = TRUE;
	const char *str;

	switch (contype) {
	case NM_OPENVPN_CONTYPE_TLS:
		is_valid = validate_tls (xml, "tls", error);
		break;

	case NM_OPENVPN_CONTYPE_PASSWORD_TLS:
		is_valid = validate_tls (xml, "pw_tls", error);
		if (!is_valid)
			break;

		widget = glade_xml_get_widget (xml, "pw_tls_username_entry");
		str = gtk_entry_get_text (GTK_ENTRY (widget));
		if (!str || !strlen (str)) {
			g_set_error (error,
			             OPENVPN_PLUGIN_UI_ERROR,
			             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
			             NM_OPENVPN_KEY_USERNAME);
			is_valid = FALSE;
		}
		break;

	case NM_OPENVPN_CONTYPE_PASSWORD:
		if (!validate_file_chooser (xml, "pw_ca_cert_chooser")) {
			g_set_error (error,
			             OPENVPN_PLUGIN_UI_ERROR,
			             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
			             NM_OPENVPN_KEY_CA);
			is_valid = FALSE;
			break;
		}
		widget = glade_xml_get_widget (xml, "pw_username_entry");
		str = gtk_entry_get_text (GTK_ENTRY (widget));
		if (!str || !strlen (str)) {
			g_set_error (error,
			             OPENVPN_PLUGIN_UI_ERROR,
			             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
			             NM_OPENVPN_KEY_USERNAME);
			is_valid = FALSE;
		}
		break;

	case NM_OPENVPN_CONTYPE_STATIC_KEY:
		if (!validate_file_chooser (xml, "sk_key_chooser")) {
			g_set_error (error,
			             OPENVPN_PLUGIN_UI_ERROR,
			             OPENVPN_PLUGIN_UI_ERROR_INVALID_PROPERTY,
			             NM_OPENVPN_KEY_SHARED_KEY);
			is_valid = FALSE;
			break;
		}
		break;

	default:
		g_assert_not_reached ();
	}

	return is_valid;
}

static void
update_from_filechooser (GladeXML *xml,
                         const char *key,
                         const char *prefix,
                         const char *widget_name,
                         NMSettingVPNProperties *s_vpn_props)
{
	GtkWidget *widget;
	char *tmp, *filename;

	g_return_if_fail (xml != NULL);
	g_return_if_fail (key != NULL);
	g_return_if_fail (prefix != NULL);
	g_return_if_fail (widget_name != NULL);
	g_return_if_fail (s_vpn_props != NULL);
	g_return_if_fail (s_vpn_props->data != NULL);

	tmp = g_strdup_printf ("%s_%s", prefix, widget_name);
	widget = glade_xml_get_widget (xml, tmp);
	g_free (tmp);

	filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
	if (!filename)
		return;

	if (strlen (filename))
		g_hash_table_insert (s_vpn_props->data, g_strdup (key), str_to_gvalue (filename));
	
	g_free (filename);
}

static void
update_tls (GladeXML *xml, const char *prefix, NMSettingVPNProperties *s_vpn_props)
{
	update_from_filechooser (xml, NM_OPENVPN_KEY_CA, prefix, "ca_cert_chooser", s_vpn_props);
	update_from_filechooser (xml, NM_OPENVPN_KEY_CERT, prefix, "user_cert_chooser", s_vpn_props);
	update_from_filechooser (xml, NM_OPENVPN_KEY_KEY, prefix, "private_key_chooser", s_vpn_props);
}

static void
update_username (GladeXML *xml, const char *prefix, NMSettingVPNProperties *s_vpn_props)
{
	GtkWidget *widget;
	char *tmp;
	const char *str;

	g_return_if_fail (xml != NULL);
	g_return_if_fail (prefix != NULL);
	g_return_if_fail (s_vpn_props != NULL);
	g_return_if_fail (s_vpn_props->data != NULL);

	tmp = g_strdup_printf ("%s_username_entry", prefix);
	widget = glade_xml_get_widget (xml, tmp);
	g_free (tmp);

	str = gtk_entry_get_text (GTK_ENTRY (widget));
	if (str && strlen (str)) {
		g_hash_table_insert (s_vpn_props->data,
		                     g_strdup (NM_OPENVPN_KEY_USERNAME),
		                     str_to_gvalue (str));
	}
}

gboolean
auth_widget_update_connection (GladeXML *xml,
                               gint contype,
                               NMSettingVPNProperties *s_vpn_props)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	GtkWidget *widget;

	switch (contype) {
	case NM_OPENVPN_CONTYPE_TLS:
		update_tls (xml, "tls", s_vpn_props);
		break;

	case NM_OPENVPN_CONTYPE_PASSWORD:
		update_from_filechooser (xml, NM_OPENVPN_KEY_CA, "pw", "ca_cert_chooser", s_vpn_props);
		update_username (xml, "pw", s_vpn_props);
		break;

	case NM_OPENVPN_CONTYPE_PASSWORD_TLS:
		update_tls (xml, "pw_tls", s_vpn_props);
		update_username (xml, "pw_tls", s_vpn_props);
		break;

	case NM_OPENVPN_CONTYPE_STATIC_KEY:
		update_from_filechooser (xml, NM_OPENVPN_KEY_SHARED_KEY, "sk", "key_chooser", s_vpn_props);
		widget = glade_xml_get_widget (xml, "sk_direction_combo");
		g_assert (widget);
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter)) {
			int direction = -1;

			gtk_tree_model_get (model, &iter, SK_DIR_COL_NUM, &direction, -1);
			if (direction > -1) {
				g_hash_table_insert (s_vpn_props->data,
				                     g_strdup (NM_OPENVPN_KEY_SHARED_KEY_DIRECTION),
				                     int_to_gvalue (direction));
			}
		}
		break;

	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static const char *
find_tag (const char *tag, const char *buf, gsize len)
{
	gsize i, taglen;

	taglen = strlen (tag);
	if (len < taglen)
		return NULL;

	for (i = 0; i < len - taglen; i++) {
		if (memcmp (buf + i, tag, taglen) == 0)
			return buf + i;
	}
	return NULL;
}

static const char *pem_rsa_key_begin = "-----BEGIN RSA PRIVATE KEY-----";
static const char *pem_dsa_key_begin = "-----BEGIN DSA PRIVATE KEY-----";
static const char *pem_cert_begin = "-----BEGIN CERTIFICATE-----";

static gboolean
tls_default_filter (const GtkFileFilterInfo *filter_info, gpointer data)
{
	char *contents = NULL, *p, *ext;
	gsize bytes_read = 0;
	gboolean show = FALSE;
	struct stat statbuf;

	if (!filter_info->filename)
		return FALSE;

	p = strrchr (filter_info->filename, '.');
	if (!p)
		return FALSE;

	ext = g_ascii_strdown (p, -1);
	if (!ext)
		return FALSE;
	if (strcmp (ext, ".pem") && strcmp (ext, ".crt") && strcmp (ext, ".key")) {
		g_free (ext);
		return FALSE;
	}
	g_free (ext);

	/* Ignore files that are really large */
	if (!stat (filter_info->filename, &statbuf)) {
		if (statbuf.st_size > 500000)
			return FALSE;
	}

	if (!g_file_get_contents (filter_info->filename, &contents, &bytes_read, NULL))
		return FALSE;

	if (bytes_read < 400)  /* needs to be lower? */
		goto out;

	/* Check for PEM signatures */
	if (find_tag (pem_rsa_key_begin, (const char *) contents, bytes_read)) {
		show = TRUE;
		goto out;
	}

	if (find_tag (pem_dsa_key_begin, (const char *) contents, bytes_read)) {
		show = TRUE;
		goto out;
	}

	if (find_tag (pem_cert_begin, (const char *) contents, bytes_read)) {
		show = TRUE;
		goto out;
	}

out:
	g_free (contents);
	return show;
}

GtkFileFilter *
tls_file_chooser_filter_new (void)
{
	GtkFileFilter *filter;

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_FILENAME, tls_default_filter, NULL, NULL);
	gtk_file_filter_set_name (filter, _("PEM certificates (*.pem, *.crt, *.key)"));
	return filter;
}


static const char *sk_key_begin = "-----BEGIN OpenVPN Static key V1-----";

static gboolean
sk_default_filter (const GtkFileFilterInfo *filter_info, gpointer data)
{
	int fd;
	unsigned char buffer[1024];
	ssize_t bytes_read;
	gboolean show = FALSE;
	char *p;
	char *ext;

	if (!filter_info->filename)
		return FALSE;

	p = strrchr (filter_info->filename, '.');
	if (!p)
		return FALSE;

	ext = g_ascii_strdown (p, -1);
	if (!ext)
		return FALSE;
	if (strcmp (ext, ".key")) {
		g_free (ext);
		return FALSE;
	}
	g_free (ext);

	fd = open (filter_info->filename, O_RDONLY);
	if (fd < 0)
		return FALSE;

	bytes_read = read (fd, buffer, sizeof (buffer) - 1);
	if (bytes_read < 400)  /* needs to be lower? */
		goto out;
	buffer[bytes_read] = '\0';

	/* Check for PEM signatures */
	if (find_tag (sk_key_begin, (const char *) buffer, bytes_read)) {
		show = TRUE;
		goto out;
	}

out:
	close (fd);
	return show;
}

GtkFileFilter *
sk_file_chooser_filter_new (void)
{
	GtkFileFilter *filter;

	filter = gtk_file_filter_new ();
	gtk_file_filter_add_custom (filter, GTK_FILE_FILTER_FILENAME, sk_default_filter, NULL, NULL);
	gtk_file_filter_set_name (filter, _("OpenVPN Static Keys (*.key)"));
	return filter;
}

static void
nm_gvalue_destroy (gpointer data)
{
	GValue *value = (GValue *) data;

	g_value_unset (value);
	g_slice_free (GValue, value);
}

static const char *advanced_keys[] = {
	NM_OPENVPN_KEY_PORT,
	NM_OPENVPN_KEY_COMP_LZO,
	NM_OPENVPN_KEY_TAP_DEV,
	NM_OPENVPN_KEY_PROTO_TCP,
	NM_OPENVPN_KEY_CIPHER,
	NM_OPENVPN_KEY_TA_DIR,
	NM_OPENVPN_KEY_TA,
	NULL
};

static void
copy_values (gpointer key, gpointer data, gpointer user_data)
{
	GHashTable *hash = (GHashTable *) user_data;
	GValue *value = (GValue *) data;
	const char **i;

	for (i = &advanced_keys[0]; *i; i++) {
		if (strcmp ((const char *) key, *i))
			continue;

		if (G_VALUE_HOLDS_STRING (value)) {
			g_hash_table_insert (hash,
			                     g_strdup ((const char *) key),
			                     str_to_gvalue (g_value_get_string (value)));
		} else if (G_VALUE_HOLDS_INT (value)) {
			g_hash_table_insert (hash,
			                     g_strdup ((const char *) key),
			                     int_to_gvalue (g_value_get_int (value)));
		} else if (G_VALUE_HOLDS_BOOLEAN (value)) {
			g_hash_table_insert (hash,
			                     g_strdup ((const char *) key),
			                     bool_to_gvalue (g_value_get_boolean (value)));
		}
	}
}

GHashTable *
advanced_dialog_new_hash_from_connection (NMConnection *connection,
                                          GError **error)
{
	GHashTable *hash;
	NMSettingVPNProperties *s_vpn_props;

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nm_gvalue_destroy);

	s_vpn_props = (NMSettingVPNProperties *) nm_connection_get_setting (connection, NM_TYPE_SETTING_VPN_PROPERTIES);
	if (s_vpn_props && s_vpn_props->data)
		g_hash_table_foreach (s_vpn_props->data, copy_values, hash);

	return hash;
}

static void
port_toggled_cb (GtkWidget *check, gpointer user_data)
{
	GladeXML *xml = (GladeXML *) user_data;
	GtkWidget *widget;

	widget = glade_xml_get_widget (xml, "port_spinbutton");
	gtk_widget_set_sensitive (widget, gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (check)));
}

static const char *
nm_find_openvpn (void)
{
	static const char *openvpn_binary_paths[] = {
		"/usr/sbin/openvpn",
		"/sbin/openvpn",
		NULL
	};
	const char  **openvpn_binary = openvpn_binary_paths;

	while (*openvpn_binary != NULL) {
		if (g_file_test (*openvpn_binary, G_FILE_TEST_EXISTS))
			break;
		openvpn_binary++;
	}

	return *openvpn_binary;
}

#define TLS_CIPHER_COL_NAME 0
#define TLS_CIPHER_COL_DEFAULT 1

static void
populate_cipher_combo (GtkComboBox *box, const char *user_cipher)
{
	GtkListStore *store;
	GtkTreeIter iter;
	const char *openvpn_binary = NULL;
	gchar *cmdline, *tmp, *token;
	gboolean user_added = FALSE;

	openvpn_binary = nm_find_openvpn ();
	if (!openvpn_binary)
		return;

	cmdline = g_strdup_printf("/bin/sh -c \"%s --show-ciphers | /bin/awk '/^[A-Z][A-Z0-9]+-/ { print $1 }'\"", openvpn_binary);
	if (!g_spawn_command_line_sync(cmdline, &tmp, NULL, NULL, NULL))
		goto end;

	token = strtok(tmp, "\n");

	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
	gtk_combo_box_set_model (box, GTK_TREE_MODEL (store));

	/* Add default option which won't pass --cipher to openvpn */
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter,
	                    TLS_CIPHER_COL_NAME, _("Default"),
	                    TLS_CIPHER_COL_DEFAULT, TRUE, -1);

	while (token) {
		if (strlen (token)) {
			gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter,
			                    TLS_CIPHER_COL_NAME, token,
			                    TLS_CIPHER_COL_DEFAULT, FALSE, -1);
			if (user_cipher && !strcmp (token, user_cipher)) {
				gtk_combo_box_set_active_iter (box, &iter);
				user_added = TRUE;
			}
		}
		token = strtok (NULL, "\n");
	}

	/* Add the user-specified cipher if it exists wasn't found by openvpn */
	if (user_cipher && !user_added) {
		gtk_list_store_insert (store, &iter, 1);
		gtk_list_store_set (store, &iter,
		                    TLS_CIPHER_COL_NAME, user_cipher,
		                    TLS_CIPHER_COL_DEFAULT, FALSE -1);
		gtk_combo_box_set_active_iter (box, &iter);
	} else if (!user_added) {
		gtk_combo_box_set_active (box, 0);
	}

	g_object_unref (G_OBJECT (store));

 end:
	g_free(tmp);
}

static void
tls_auth_toggled_cb (GtkWidget *widget, gpointer user_data)
{
	GladeXML *xml = (GladeXML *) user_data;
	gboolean use_auth = FALSE;

	widget = glade_xml_get_widget (xml, "tls_auth_checkbutton");
	use_auth = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

	widget = glade_xml_get_widget (xml, "tls_dir_help_label");
	gtk_widget_set_sensitive (widget, use_auth);
	widget = glade_xml_get_widget (xml, "direction_label");
	gtk_widget_set_sensitive (widget, use_auth);
	widget = glade_xml_get_widget (xml, "tls_auth_label");
	gtk_widget_set_sensitive (widget, use_auth);
	widget = glade_xml_get_widget (xml, "tls_auth_chooser");
	gtk_widget_set_sensitive (widget, use_auth);
	widget = glade_xml_get_widget (xml, "direction_combo");
	gtk_widget_set_sensitive (widget, use_auth);
}

#define TA_DIR_COL_NAME 0
#define TA_DIR_COL_NUM 1

GtkWidget *
advanced_dialog_new (GHashTable *hash, int contype)
{
	GladeXML *xml;
	GtkWidget *dialog = NULL;
	char *glade_file = NULL;
	GtkWidget *widget;
	GValue *value;

	g_return_val_if_fail (hash != NULL, NULL);

	glade_file = g_strdup_printf ("%s/%s", GLADEDIR, "nm-openvpn-dialog.glade");
	xml = glade_xml_new (glade_file, "openvpn-advanced-dialog", GETTEXT_PACKAGE);
	if (xml == NULL)
		goto out;

	dialog = glade_xml_get_widget (xml, "openvpn-advanced-dialog");
	if (!dialog) {
		g_object_unref (G_OBJECT (xml));
		goto out;
	}
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	g_object_set_data_full (G_OBJECT (dialog), "glade-xml",
	                        xml, (GDestroyNotify) g_object_unref);
	g_object_set_data (G_OBJECT (dialog), "connection-type", GINT_TO_POINTER (contype));

	widget = glade_xml_get_widget (xml, "port_checkbutton");
	g_signal_connect (G_OBJECT (widget), "toggled", G_CALLBACK (port_toggled_cb), xml);

	value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_PORT);
	if (value && G_VALUE_HOLDS_INT (value)) {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);

		widget = glade_xml_get_widget (xml, "port_spinbutton");
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget),
		                           (gdouble) g_value_get_int (value));
		gtk_widget_set_sensitive (widget, TRUE);
	} else {
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), FALSE);

		widget = glade_xml_get_widget (xml, "port_spinbutton");
		gtk_spin_button_set_value (GTK_SPIN_BUTTON (widget), 1194.0);
		gtk_widget_set_sensitive (widget, FALSE);
	}

	value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_COMP_LZO);
	if (value && G_VALUE_HOLDS_BOOLEAN (value)) {
		widget = glade_xml_get_widget (xml, "lzo_checkbutton");
		if (g_value_get_boolean (value))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
	}

	value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_PROTO_TCP);
	if (value && G_VALUE_HOLDS_BOOLEAN (value)) {
		widget = glade_xml_get_widget (xml, "tcp_checkbutton");
		if (g_value_get_boolean (value))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
	}

	value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_TAP_DEV);
	if (value && G_VALUE_HOLDS_BOOLEAN (value)) {
		widget = glade_xml_get_widget (xml, "tap_checkbutton");
		if (g_value_get_boolean (value))
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
	}

	if (contype != NM_OPENVPN_CONTYPE_TLS && contype != NM_OPENVPN_CONTYPE_PASSWORD_TLS) {
		widget = glade_xml_get_widget (xml, "options_notebook");
		gtk_notebook_remove_page (GTK_NOTEBOOK (widget), 1);
	} else {
		char *user_cipher = NULL;
		GtkListStore *store;
		GtkTreeIter iter;
		int direction = -1, active = -1;

		widget = glade_xml_get_widget (xml, "cipher_combo");
		value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_CIPHER);
		if (value && G_VALUE_HOLDS_STRING (value))
			user_cipher = (char *) g_value_get_string (value);
		populate_cipher_combo (GTK_COMBO_BOX (widget), user_cipher);

		widget = glade_xml_get_widget (xml, "tls_auth_checkbutton");
		value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_TA);
		if (value && G_VALUE_HOLDS_STRING (value)) {
			if (strlen (g_value_get_string (value)))
				gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (widget), TRUE);
		}
		g_signal_connect (G_OBJECT (widget), "toggled", G_CALLBACK (tls_auth_toggled_cb), xml);
		tls_auth_toggled_cb (widget, xml);

		widget = glade_xml_get_widget (xml, "direction_combo");
		value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_TA_DIR);
		if (value && G_VALUE_HOLDS_STRING (value)) {
			direction = (int) strtol (g_value_get_string (value), NULL, 10);
			/* If direction is not 0 or 1, use no direction */
			if (direction != 0 && direction != 1)
				direction = -1;
		}

		store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, TA_DIR_COL_NAME, _("None"), TA_DIR_COL_NUM, -1, -1);

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, TA_DIR_COL_NAME, "0", TA_DIR_COL_NUM, 0, -1);
		if (direction == 0)
			active = 1;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, TA_DIR_COL_NAME, "1", TA_DIR_COL_NUM, 1, -1);
		if (direction == 1)
			active = 2;

		gtk_combo_box_set_model (GTK_COMBO_BOX (widget), GTK_TREE_MODEL (store));
		g_object_unref (store);
		gtk_combo_box_set_active (GTK_COMBO_BOX (widget), active < 0 ? 0 : active);

		value = g_hash_table_lookup (hash, NM_OPENVPN_KEY_TA);
		if (value && G_VALUE_HOLDS_STRING (value)) {
			widget = glade_xml_get_widget (xml, "tls_auth_chooser");
			gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (widget), g_value_get_string (value));
		}
	}

out:
	g_free (glade_file);
	return dialog;
}

GHashTable *
advanced_dialog_new_hash_from_dialog (GtkWidget *dialog, GError **error)
{
	GHashTable *hash;
	GtkWidget *widget;
	GladeXML *xml;
	int contype = NM_OPENVPN_CONTYPE_INVALID;

	g_return_val_if_fail (dialog != NULL, NULL);
	if (error)
		g_return_val_if_fail (*error == NULL, NULL);

	xml = g_object_get_data (G_OBJECT (dialog), "glade-xml");
	g_return_val_if_fail (xml != NULL, NULL);

	hash = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, nm_gvalue_destroy);

	widget = glade_xml_get_widget (xml, "port_checkbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
		int port;

		widget = glade_xml_get_widget (xml, "port_spinbutton");
		port = gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON (widget));
		g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_PORT), int_to_gvalue (port));
	}

	widget = glade_xml_get_widget (xml, "lzo_checkbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_COMP_LZO), bool_to_gvalue (TRUE));

	widget = glade_xml_get_widget (xml, "tcp_checkbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_PROTO_TCP), bool_to_gvalue (TRUE));

	widget = glade_xml_get_widget (xml, "tap_checkbutton");
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
		g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_TAP_DEV), bool_to_gvalue (TRUE));

	contype = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), "connection-type"));
	if (contype == NM_OPENVPN_CONTYPE_TLS || contype == NM_OPENVPN_CONTYPE_PASSWORD_TLS) {
		GtkTreeModel *model;
		GtkTreeIter iter;

		widget = glade_xml_get_widget (xml, "cipher_combo");
		model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
		if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter)) {
			char *cipher = NULL;
			gboolean is_default = TRUE;

			gtk_tree_model_get (model, &iter,
			                    TLS_CIPHER_COL_NAME, &cipher,
			                    TLS_CIPHER_COL_DEFAULT, &is_default, -1);
			if (!is_default && cipher) {
				g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_CIPHER),
				                     str_to_gvalue (cipher));
			}
		}
		
		widget = glade_xml_get_widget (xml, "tls_auth_checkbutton");
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) {
			char *filename;

			widget = glade_xml_get_widget (xml, "tls_auth_chooser");
			filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (widget));
			if (filename && strlen (filename)) {
				g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_TA),
				                     str_to_gvalue (filename));
			}
			g_free (filename);

			widget = glade_xml_get_widget (xml, "direction_combo");
			model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
			if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter)) {
				int direction = -1;

				gtk_tree_model_get (model, &iter, TA_DIR_COL_NUM, &direction, -1);
				if (direction >= 0) {
					char str_dir[2] = { '0', '\0' };

					str_dir[0] = (direction == 0) ? '0' : '1';
					g_hash_table_insert (hash, g_strdup (NM_OPENVPN_KEY_TA_DIR),
					                     str_to_gvalue (str_dir));					
				}
			}
		}
	}

	return hash;
}

