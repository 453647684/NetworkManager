/* NetworkManagerInfo -- Manage allowed access points and provide a UI
 *                         for WEP key entry
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
 * (C) Copyright 2004 Red Hat, Inc.
 */

#include <glib.h>
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "NetworkManagerInfo.h"
#include "NetworkManagerInfoDbus.h"
#include "NetworkManagerInfoPassphraseDialog.h"
#include "NetworkManagerInfoVPN.h"


/*
 * nmi_show_warning_dialog
 *
 * pop up a warning or error dialog with certain text
 *
 */
static void nmi_show_warning_dialog (gboolean error, gchar *mesg, ...)
{
	GtkWidget	*dialog;
	char		*tmp;
	va_list	 ap;

	va_start (ap,mesg);
	tmp = g_strdup_vprintf (mesg,ap);
	dialog = gtk_message_dialog_new (NULL, 0, error ? GTK_MESSAGE_ERROR : GTK_MESSAGE_WARNING,
					 GTK_BUTTONS_OK, mesg, NULL);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	g_free (tmp);
	va_end (ap);
}


/*
 * nmi_network_type_valid
 *
 * Helper to validate network types NMI can deal with
 *
 */
inline gboolean nmi_network_type_valid (NMNetworkType type)
{
	return ((type == NETWORK_TYPE_ALLOWED));
}


/*
 * nmi_dbus_create_error_message
 *
 * Make a DBus error message
 *
 */
static DBusMessage *nmi_dbus_create_error_message (DBusMessage *message, const char *exception_namespace,
										const char *exception, const char *format, ...)
{
	DBusMessage	*reply_message;
	va_list		 args;
	char			 error_text[512];


	va_start (args, format);
	vsnprintf (error_text, 512, format, args);
	va_end (args);

	char *exception_text = g_strdup_printf ("%s.%s", exception_namespace, exception);
	reply_message = dbus_message_new_error (message, exception_text, error_text);
	g_free (exception_text);

	return (reply_message);
}


/*
 * nmi_dbus_get_key_for_network
 *
 * Throw up the user key dialog
 *
 */
static void nmi_dbus_get_key_for_network (NMIAppInfo *info, DBusMessage *message)
{
	DBusError			 error;
	char				*device = NULL;
	char				*network = NULL;
	int				 attempt = 0;

	dbus_error_init (&error);
	if (dbus_message_get_args (message, &error,
							DBUS_TYPE_STRING, &device,
							DBUS_TYPE_STRING, &network,
							DBUS_TYPE_INT32, &attempt,
							DBUS_TYPE_INVALID))
	{
		nmi_passphrase_dialog_show (device, network, info);

		dbus_free (device);
		dbus_free (network);
	}
}

/*
 * nmi_dbus_get_user_pass
 *
 * Request a username/password for VPN login
 *
 */
static void nmi_dbus_get_vpn_userpass (NMIAppInfo *info, DBusMessage *message)
{
	DBusError			 error;
	char				*vpn;
	char				*username;
	dbus_bool_t			 retry;


	dbus_error_init (&error);
	if (dbus_message_get_args (message, &error,
				   DBUS_TYPE_STRING, &vpn,
				   DBUS_TYPE_STRING, &username,
				   DBUS_TYPE_BOOLEAN, &retry,
				   DBUS_TYPE_INVALID))
	{
		if (username[0] == '\0') {
			dbus_free (username);
			username = NULL;
		}
		nmi_vpn_request_password (info, message, vpn, username, retry);
		dbus_free (vpn);
		dbus_free (username);
	}
}


/*
 * nmi_dbus_dbus_return_user_key
 *
 * Alert NetworkManager of the new user key
 *
 */
void nmi_dbus_return_user_key (DBusConnection *connection, const char *device,
						const char *network, const char *passphrase, const int key_type)
{
	DBusMessage	*message;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (device != NULL);
	g_return_if_fail (network != NULL);
	g_return_if_fail (passphrase != NULL);

	if (!(message = dbus_message_new_method_call (NM_DBUS_SERVICE, NM_DBUS_PATH, NM_DBUS_INTERFACE, "setKeyForNetwork")))
	{
		syslog (LOG_ERR, "nmi_dbus_return_user_key(): Couldn't allocate the dbus message");
		return;
	}

	/* Add network name and passphrase */
	if (dbus_message_append_args (message, DBUS_TYPE_STRING, device,
								DBUS_TYPE_STRING, network,
								DBUS_TYPE_STRING, passphrase,
								DBUS_TYPE_INT32, key_type,
								DBUS_TYPE_INVALID))
	{
		if (!dbus_connection_send (connection, message, NULL))
			syslog (LOG_ERR, "nmi_dbus_return_user_key(): dbus could not send the message");
	}

	dbus_message_unref (message);
}

/*
 * nmi_dbus_return_userpass
 *
 * Alert caller of the username/password
 *
 */
void nmi_dbus_return_vpn_password (DBusConnection *connection, DBusMessage *message, const char *password)
{
	DBusMessage	*reply;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (message != NULL);
	g_return_if_fail (password != NULL);

	if (password == NULL)
	{
		reply = dbus_message_new_error (message, NMI_DBUS_INTERFACE ".Cancelled", "Operation cancelled by user");
	}
	else
	{
		reply = dbus_message_new_method_return (message);
		dbus_message_append_args (reply,
					  DBUS_TYPE_STRING, password,
					  DBUS_TYPE_INVALID);
	}
	dbus_connection_send (connection, reply, NULL);
	dbus_message_unref (reply);
	dbus_message_unref (message);
}

/*
 * nmi_dbus_signal_update_network
 *
 * Signal NetworkManager that it needs to update info associated with a particular
 * allowed/ignored network.
 *
 */
void nmi_dbus_signal_update_network (DBusConnection *connection, const char *network, NMNetworkType type)
{
	DBusMessage		*message;

	g_return_if_fail (connection != NULL);
	g_return_if_fail (network != NULL);

	if (type != NETWORK_TYPE_ALLOWED)
		return;

	message = dbus_message_new_signal (NMI_DBUS_PATH, NMI_DBUS_INTERFACE, "WirelessNetworkUpdate");
	if (!message)
	{
		syslog (LOG_ERR, "nmi_dbus_signal_update_network(): Not enough memory for new dbus message!");
		return;
	}

	dbus_message_append_args (message, DBUS_TYPE_STRING, network, DBUS_TYPE_INVALID);
	if (!dbus_connection_send (connection, message, NULL))
		syslog (LOG_WARNING, "nmi_dbus_signal_update_network(): Could not raise the 'WirelessNetworkUpdate' signal!");

	dbus_message_unref (message);
}


/*
 * nmi_dbus_get_networks
 *
 * Grab a list of access points from GConf and return it in the form
 * of a string array in a dbus message.
 *
 */
static DBusMessage *nmi_dbus_get_networks (NMIAppInfo *info, DBusMessage *message)
{
	GSList			*dir_list = NULL;
	GSList			*element = NULL;
	DBusError			 error;
	DBusMessage		*reply_message = NULL;
	DBusMessageIter	 iter;
	DBusMessageIter	 iter_array;
	NMNetworkType		 type;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (	   !dbus_message_get_args (message, &error, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworks called with invalid arguments.");
		return (reply_message);
	}

	/* List all allowed access points that gconf knows about */
	element = dir_list = gconf_client_all_dirs (info->gconf_client, NMI_GCONF_WIRELESS_NETWORKS_PATH, NULL);
	if (!dir_list)
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "NoNetworks",
							"There were are no wireless networks stored.");
	}
	else
	{
		gboolean	value_added = FALSE;

		reply_message = dbus_message_new_method_return (message);
		dbus_message_iter_init (reply_message, &iter);
		dbus_message_iter_append_array (&iter, &iter_array, DBUS_TYPE_STRING);

		/* Append the essid of every allowed or ignored access point we know of 
		 * to a string array in the dbus message.
		 */
		while (element)
		{
			char			 key[100];
			GConfValue	*value;

			g_snprintf (&key[0], 99, "%s/essid", (char *)(element->data));
			value = gconf_client_get (info->gconf_client, key, NULL);
			if (value && gconf_value_get_string (value))
			{
				dbus_message_iter_append_string (&iter_array, gconf_value_get_string (value));
				value_added = TRUE;
				gconf_value_free (value);
			}

			g_free (element->data);
			element = element->next;
		}
		g_slist_free (dir_list);

		if (!value_added)
		{
			dbus_message_unref (reply_message);
			reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "NoNetworks",
							"There were are no wireless networks stored.");
		}
	}

	return (reply_message);
}
 

/*
 * nmi_dbus_get_network_timestamp
 *
 * If the specified network exists, get its timestamp from gconf
 * and pass it back as a dbus message.
 *
 */
static DBusMessage *nmi_dbus_get_network_timestamp (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	gchar			*key = NULL;
	char				*network = NULL;
	GConfValue		*value;
	DBusError			 error;
	NMNetworkType		 type;
	char				*escaped_network;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworkTimestamp called with invalid arguments.");
		return (reply_message);
	}

	/* Grab timestamp key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/timestamp", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	g_free (escaped_network);
	value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);

	if (value)
	{
		reply_message = dbus_message_new_method_return (message);
		dbus_message_append_args (reply_message, DBUS_TYPE_INT32, gconf_value_get_int (value), DBUS_TYPE_INVALID);
		gconf_value_free (value);
	}
	else
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "BadNetworkData",
							"NetworkManagerInfo::getNetworkTimestamp could not access data for network '%s'", network);
	}

	dbus_free (network);
	return (reply_message);
}


/*
 * nmi_dbus_get_network_essid
 *
 * If the specified network exists, get its essid from gconf
 * and pass it back as a dbus message.
 *
 */
static DBusMessage *nmi_dbus_get_network_essid (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	gchar			*key = NULL;
	char				*network = NULL;
	GConfValue		*value;
	DBusError			 error;
	NMNetworkType		 type;
	char				*escaped_network;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworkEssid called with invalid arguments.");
		return (reply_message);
	}

	/* Grab essid key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/essid", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	g_free (escaped_network);
	value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);

	if (value)
	{
		reply_message = dbus_message_new_method_return (message);
		dbus_message_append_args (reply_message, DBUS_TYPE_STRING, gconf_value_get_string (value), DBUS_TYPE_INVALID);
		gconf_value_free (value);
	}
	else
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "BadNetworkData",
							"NetworkManagerInfo::getNetworkEssid could not access data for network '%s'", network);
	}

	dbus_free (network);
	return (reply_message);
}


/*
 * nmi_dbus_get_network_key
 *
 * If the specified network exists, get its key and key type from gconf
 * and pass it back as a dbus message.
 *
 */
static DBusMessage *nmi_dbus_get_network_key (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	gchar			*key = NULL;
	char				*network = NULL;
	GConfValue		*key_value;
	GConfValue		*key_type_value;
	DBusError			 error;
	NMNetworkType		 type;
	char				*escaped_network;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworkKey called with invalid arguments.");
		return (reply_message);
	}

	/* Grab user-key key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/key", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	key_value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);

	key = g_strdup_printf ("%s/%s/key_type", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	key_type_value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);
	g_free (escaped_network);

	/* We don't error out if no key was found in gconf, we return blank key */
	reply_message = dbus_message_new_method_return (message);
	if (key_value && key_type_value)
	{
		dbus_message_append_args (reply_message, DBUS_TYPE_STRING, gconf_value_get_string (key_value),
								DBUS_TYPE_INT32, gconf_value_get_int (key_type_value), DBUS_TYPE_INVALID);
	}
	else
		dbus_message_append_args (reply_message, DBUS_TYPE_STRING, "", DBUS_TYPE_INT32, -1, DBUS_TYPE_INVALID);

	if (key_value)
		gconf_value_free (key_value);
	if (key_type_value)
		gconf_value_free (key_type_value);

	return (reply_message);
}


/*
 * nmi_dbus_get_network_trusted
 *
 * If the specified network exists, get its "trusted" value
 * from gconf and pass it back.
 *
 */
static DBusMessage *nmi_dbus_get_network_trusted (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	gchar			*key = NULL;
	char				*network = NULL;
	GConfValue		*value;
	DBusError			 error;
	NMNetworkType		 type;
	char				*escaped_network;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworkTrusted called with invalid arguments.");
		return (reply_message);
	}

	/* Grab user-key key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/trusted", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	g_free (escaped_network);
	value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);

	/* We don't error out if no key was found in gconf, we return blank key */
	reply_message = dbus_message_new_method_return (message);
	if (value)
	{
		dbus_message_append_args (reply_message, DBUS_TYPE_BOOLEAN, gconf_value_get_bool (value), DBUS_TYPE_INVALID);
		gconf_value_free (value);
	}
	else
		dbus_message_append_args (reply_message, DBUS_TYPE_BOOLEAN, FALSE, DBUS_TYPE_INVALID);

	return (reply_message);
}


/*
 * nmi_dbus_get_network_addresses
 *
 * If the specified network exists, grabs a list of AP MAC addresses
 * from gconf and pass it back.
 *
 */
static DBusMessage *nmi_dbus_get_network_addresses (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	char				*network = NULL;
	NMNetworkType		 type;
	char				*key;
	GConfValue		*value;
	DBusError			 error;
	char				*escaped_network;
	gboolean			 success = FALSE;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::getNetworkAddresses called with invalid arguments.");
		return (reply_message);
	}

	/* Grab user-key key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/addresses", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	g_free (escaped_network);
	value = gconf_client_get (info->gconf_client, key, NULL);
	g_free (key);

	/* We don't error out if no key was found in gconf, we return blank key */
	reply_message = dbus_message_new_method_return (message);
	if (value && (value->type == GCONF_VALUE_LIST) && (gconf_value_get_list_type (value) == GCONF_VALUE_STRING))
	{
		DBusMessageIter	 iter;
		DBusMessageIter	 iter_array;
		GSList			*list = gconf_value_get_list (value);
		GSList			*elem = list;

		dbus_message_iter_init (reply_message, &iter);
		dbus_message_iter_append_array (&iter, &iter_array, DBUS_TYPE_STRING);

		while (elem != NULL)
		{
			const char *string = gconf_value_get_string ((GConfValue *)elem->data);
			if (string)
			{
				dbus_message_iter_append_string (&iter_array, string);
				success = TRUE;
			}
			elem = g_slist_next (elem);
		}
	}
	gconf_value_free (value);

	if (!success)
	{
		dbus_message_unref (reply_message);
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "NoAddresses",
						"There were no stored addresses for this wireless network.");
	}

	return (reply_message);
}


/*
 * nmi_dbus_add_network_address
 *
 * Add an AP's MAC address to a wireless network entry in gconf
 *
 */
static DBusMessage *nmi_dbus_add_network_address (NMIAppInfo *info, DBusMessage *message)
{
	DBusMessage		*reply_message = NULL;
	char				*network = NULL;
	NMNetworkType		 type;
	char				*addr;
	char				*key;
	GConfValue		*value;
	DBusError			 error;
	char				*escaped_network;
	GSList			*new_mac_list = NULL;
	gboolean			 found = FALSE;

	g_return_val_if_fail (info != NULL, NULL);
	g_return_val_if_fail (message != NULL, NULL);

	dbus_error_init (&error);
	if (    !dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INT32, &type, DBUS_TYPE_STRING, &addr, DBUS_TYPE_INVALID)
		|| !nmi_network_type_valid (type)
		|| (strlen (network) <= 0)
		|| !addr
		|| (strlen (addr) < 11))
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "InvalidArguments",
							"NetworkManagerInfo::addNetworkAddress called with invalid arguments.");
		return (reply_message);
	}

	/* Grab user-key key for our access point from GConf */
	escaped_network = gconf_escape_key (network, strlen (network));
	key = g_strdup_printf ("%s/%s/addresses", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	g_free (escaped_network);
	value = gconf_client_get (info->gconf_client, key, NULL);

	if (value && (value->type == GCONF_VALUE_LIST) && (gconf_value_get_list_type (value) == GCONF_VALUE_STRING))
	{
		GSList	*elem;

		new_mac_list = gconf_client_get_list (info->gconf_client, key, GCONF_VALUE_STRING, NULL);
		gconf_value_free (value);

		/* Ensure that the MAC isn't already in the list */
		elem = new_mac_list;
		while (elem)
		{
			if (elem->data && !strcmp (addr, elem->data))
			{
				found = TRUE;
				break;
			}
			elem = g_slist_next (elem);
		}
	}

	/* Add the new MAC address to the end of the list */
	if (!found)
	{
		new_mac_list = g_slist_append (new_mac_list, g_strdup (addr));
		gconf_client_set_list (info->gconf_client, key, GCONF_VALUE_STRING, new_mac_list, NULL);
	}

	/* Free the list, since gconf_client_set_list deep-copies it */
	g_slist_foreach (new_mac_list, (GFunc)g_free, NULL);
	g_slist_free (new_mac_list);

	dbus_free (addr);
	g_free (key);

	return (NULL);
}


/*
 * nmi_dbus_nmi_message_handler
 *
 * Responds to requests for our services
 *
 */
static DBusHandlerResult nmi_dbus_nmi_message_handler (DBusConnection *connection, DBusMessage *message, void *user_data)
{
	const char		*method;
	const char		*path;
	NMIAppInfo		*info = (NMIAppInfo *)user_data;
	DBusMessage		*reply_message = NULL;

	g_return_val_if_fail (info != NULL, DBUS_HANDLER_RESULT_HANDLED);

	method = dbus_message_get_member (message);
	path = dbus_message_get_path (message);

/*	syslog (LOG_WARNING, "nmi_dbus_nmi_message_handler() got method %s for path %s", method, path);*/

	if (strcmp ("getKeyForNetwork", method) == 0)
	{
		GtkWidget	*dialog = glade_xml_get_widget (info->passphrase_dialog, "passphrase_dialog");
		if (!GTK_WIDGET_VISIBLE (dialog))
			nmi_dbus_get_key_for_network (info, message);
	}
	else if (strcmp ("cancelGetKeyForNetwork", method) == 0)
	{
		GtkWidget	*dialog = glade_xml_get_widget (info->passphrase_dialog, "passphrase_dialog");
		if (GTK_WIDGET_VISIBLE (dialog))
			nmi_passphrase_dialog_cancel (info);
	}
	else if (strcmp ("getVPNUserPass", method) == 0)
	{
		nmi_dbus_get_vpn_userpass (info, message);
	}
	else if (strcmp ("networkNotFound", method) == 0)
	{
		char		*network;
		DBusError	 error;

		dbus_error_init (&error);
		if (dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &network, DBUS_TYPE_INVALID))
		{
			GtkDialog	*dialog;
			char		*text;

			text = g_strdup_printf ( "The requested wireless network '%s' does not appear to be in range.  "
								"A different wireless network will be used if any are available.", network);
			dbus_free (network);

			dialog = GTK_DIALOG (gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, text, NULL));
			gtk_dialog_run (dialog);
			gtk_widget_destroy (GTK_WIDGET (dialog));
			dbus_error_free (&error);
		}
	}
	else if (strcmp ("getNetworks", method) == 0)
		reply_message = nmi_dbus_get_networks (info, message);
	else if (strcmp ("getNetworkTimestamp", method) == 0)
		reply_message = nmi_dbus_get_network_timestamp (info, message);
	else if (strcmp ("getNetworkEssid", method) == 0)
		reply_message = nmi_dbus_get_network_essid (info, message);
	else if (strcmp ("getNetworkKey", method) == 0)
		reply_message = nmi_dbus_get_network_key (info, message);
	else if (strcmp ("getNetworkTrusted", method) == 0)
		reply_message = nmi_dbus_get_network_trusted (info, message);
	else if (strcmp ("getNetworkAddresses", method) == 0)
		reply_message = nmi_dbus_get_network_addresses (info, message);
	else if (strcmp ("addNetworkAddress", method) == 0)
		nmi_dbus_add_network_address (info, message);
	else
	{
		reply_message = nmi_dbus_create_error_message (message, NMI_DBUS_INTERFACE, "UnknownMethod",
							"NetworkManagerInfo knows nothing about the method %s for object %s", method, path);
	}

	if (reply_message)
	{
		dbus_connection_send (connection, reply_message, NULL);
		dbus_message_unref (reply_message);
	}

	return (DBUS_HANDLER_RESULT_HANDLED);
}


/*
 * nmi_dbus_nmi_unregister_handler
 *
 * Nothing happens here.
 *
 */
void nmi_dbus_nmi_unregister_handler (DBusConnection *connection, void *user_data)
{
	/* do nothing */
}

gboolean shutdown_callback (gpointer data)
{
	gtk_main_quit ();
	return FALSE;
}

static DBusHandlerResult nmi_dbus_filter (DBusConnection *connection, DBusMessage *message, void *user_data)
{
	char			*ap_path;
	char			*dev_path;
	DBusError		 error;
	gboolean		 handled = FALSE;
	NMIAppInfo	*info = (NMIAppInfo *) user_data;
	gboolean		 appeared = FALSE;
	gboolean		 disappeared = FALSE;

	g_return_val_if_fail (info != NULL, DBUS_HANDLER_RESULT_NOT_YET_HANDLED);

	if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "WirelessNetworkAppeared"))
		appeared = TRUE;
	else if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "WirelessNetworkDisappeared"))
		disappeared = TRUE;
	else if (dbus_message_is_signal (message, DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS, "ServiceDeleted"))
	{
		char 	*service;
		DBusError	 error;

		dbus_error_init (&error);
		if (dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID))
		{
			if (strcmp (service, NM_DBUS_SERVICE) == 0)
			{
				if (info->shutdown_timeout != NULL) 
					g_source_destroy (info->shutdown_timeout);

				info->shutdown_timeout = g_timeout_source_new (30000);
				if (info->shutdown_timeout != NULL)
				{
					g_source_set_callback (info->shutdown_timeout,
							       shutdown_callback,
							       info,
							       NULL);

					g_source_attach (info->shutdown_timeout, NULL);
				}
			}
		}

		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
	}
	else if (dbus_message_is_signal (message, DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS, "ServiceCreated"))
	{
		char 	*service;
		DBusError	 error;

		dbus_error_init (&error);
		if (dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &service, DBUS_TYPE_INVALID))
		{
			if (strcmp (service, NM_DBUS_SERVICE) == 0 && 
			    info->shutdown_timeout != NULL)
			{
				g_source_destroy (info->shutdown_timeout);
				info->shutdown_timeout = NULL;
			}	
		}

		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
	}
	else if (dbus_message_is_signal (message, NM_DBUS_INTERFACE, "DeviceActivationFailed"))
	{
		char		*dev = NULL;
		char		*net = NULL;
		DBusError	 error;

		dbus_error_init (&error);
		if (!dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &dev, DBUS_TYPE_STRING, &net, DBUS_TYPE_INVALID))
		{
			if (dbus_error_is_set (&error))
				dbus_error_free (&error);
			dbus_error_init (&error);
			dbus_message_get_args (message, &error, DBUS_TYPE_STRING, &dev, DBUS_TYPE_INVALID);
		}
		if (dbus_error_is_set (&error))
			dbus_error_free (&error);
		if (dev && net)
		{
			char *string = g_strdup_printf ("Connection to the wireless network '%s' failed.\n", net);
			nmi_show_warning_dialog (TRUE, string);
			g_free (string);
		}
		else if (dev)
			nmi_show_warning_dialog (TRUE, "Connection to the wired network failed.\n");

		dbus_free (dev);
		dbus_free (net);
	}

	if (appeared || disappeared)
	{
		dbus_error_init (&error);
		if (dbus_message_get_args (message, &error,
								DBUS_TYPE_STRING, &dev_path,
								DBUS_TYPE_STRING, &ap_path,
								DBUS_TYPE_INVALID))
		{
#if 0
			if (appeared)
				nmi_new_networks_dialog_add_network (ap_path, info);
			else if (disappeared)
				nmi_new_networks_dialog_add_network (ap_path, info);
#endif

			dbus_free (dev_path);
			dbus_free (ap_path);
			handled = TRUE;
		}
	}

	return (handled ? DBUS_HANDLER_RESULT_HANDLED : DBUS_HANDLER_RESULT_NOT_YET_HANDLED);
}

/*
 * nmi_dbus_nm_is_running
 *
 * Ask dbus whether or not NetworkManager is running
 *
 */
static gboolean nmi_dbus_nm_is_running (DBusConnection *connection)
{
	DBusError		error;
	gboolean		exists;

	g_return_val_if_fail (connection != NULL, FALSE);

	dbus_error_init (&error);
	exists = dbus_bus_service_exists (connection, NM_DBUS_SERVICE, &error);
	if (dbus_error_is_set (&error))
		dbus_error_free (&error);
	return (exists);
}

/*
 * nmi_dbus_service_init
 *
 * Connect to the system messagebus and register ourselves as a service.
 *
 */
int nmi_dbus_service_init (DBusConnection *dbus_connection, NMIAppInfo *info)
{
	DBusError		 		 dbus_error;
	DBusObjectPathVTable	 nmi_vtable = { &nmi_dbus_nmi_unregister_handler, &nmi_dbus_nmi_message_handler, NULL, NULL, NULL, NULL };
	int acquisition;

	dbus_error_init (&dbus_error);
	acquisition = dbus_bus_acquire_service (dbus_connection, NMI_DBUS_SERVICE,
						DBUS_SERVICE_FLAG_PROHIBIT_REPLACEMENT,
						&dbus_error);
	if (dbus_error_is_set (&dbus_error))
	{
		syslog (LOG_ERR, "nmi_dbus_service_init() could not acquire its service.  dbus_bus_acquire_service() says: '%s'", dbus_error.message);
		dbus_error_free (&dbus_error);
		return (-1);
	}
	if (acquisition & DBUS_SERVICE_REPLY_SERVICE_EXISTS) {
	     exit (0);
	}

	if (!nmi_dbus_nm_is_running (dbus_connection))
		return (-1);

	if (!dbus_connection_register_object_path (dbus_connection, NMI_DBUS_PATH, &nmi_vtable, info))
	{
		syslog (LOG_ERR, "nmi_dbus_service_init() could not register a handler for NetworkManagerInfo.  Not enough memory?");
		return (-1);
	}

	if (!dbus_connection_add_filter (dbus_connection, nmi_dbus_filter, info, NULL))
		return (-1);

	dbus_error_init (&dbus_error);
	dbus_bus_add_match (dbus_connection,
				"type='signal',"
				"interface='" NM_DBUS_INTERFACE "',"
				"sender='" NM_DBUS_SERVICE "',"
				"path='" NM_DBUS_PATH "'", &dbus_error);
	if (dbus_error_is_set (&dbus_error))
	{
		dbus_error_free (&dbus_error);
		return (-1);
	}

	dbus_bus_add_match(dbus_connection,
				"type='signal',"
				"interface='" DBUS_INTERFACE_ORG_FREEDESKTOP_DBUS "',"
				"sender='" DBUS_SERVICE_ORG_FREEDESKTOP_DBUS "'",
				&dbus_error);
	if (dbus_error_is_set (&dbus_error))
	{
		dbus_error_free (&dbus_error);
		return (-1);
	}

	return (0);
}
