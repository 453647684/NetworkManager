/* NetworkManager Wireless Applet -- Display wireless access points and allow user control
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
 * This applet used the GNOME Wireless Applet as a skeleton to build from.
 *
 * GNOME Wireless Applet Authors:
 *		Eskil Heyn Olsen <eskil@eskil.dk>
 *		Bastien Nocera <hadess@hadess.net> (Gnome2 port)
 *
 * (C) Copyright 2004-2005 Red Hat, Inc.
 * (C) Copyright 2001, 2002 Free Software Foundation
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <dirent.h>

#include <gnome.h>

#include <glib/gi18n.h>
#include <glade/glade.h>
#include <gconf/gconf-client.h>

#include "NMWirelessApplet.h"
#include "NMWirelessAppletDbus.h"
#include "NMWirelessAppletOtherNetworkDialog.h"
#include "menu-info.h"

#define CFG_UPDATE_INTERVAL 1

/* Compat for GTK 2.4 and lower... */
#if (GTK_MAJOR_VERSION <= 2 && GTK_MINOR_VERSION < 6)
	#define GTK_STOCK_MEDIA_PAUSE		GTK_STOCK_STOP
	#define GTK_STOCK_MEDIA_PLAY		GTK_STOCK_REFRESH
	#define GTK_STOCK_ABOUT			GTK_STOCK_DIALOG_INFO
#endif


static GtkWidget *	nmwa_populate_menu	(NMWirelessApplet *applet);
static void		nmwa_dispose_menu_items (NMWirelessApplet *applet);
static gboolean	nmwa_toplevel_menu_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static GObject *	nmwa_constructor (GType type, guint n_props, GObjectConstructParam *construct_props);
static void		setup_stock (void);
static void		nmwa_icons_init (NMWirelessApplet *applet);
static gboolean	nmwa_fill (NMWirelessApplet *applet);
static void		nmwa_about_cb (NMWirelessApplet *applet);
static void		nmwa_context_menu_update (NMWirelessApplet *applet);

G_DEFINE_TYPE(NMWirelessApplet, nmwa, EGG_TYPE_TRAY_ICON)

static void
nmwa_init (NMWirelessApplet *applet)
{
  applet->animation_id = 0;
  applet->animation_step = 0;

  setup_stock ();
  nmwa_icons_init (applet);
  nmwa_fill (applet);
}

static void nmwa_class_init (NMWirelessAppletClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructor = nmwa_constructor;
}

static GObject *nmwa_constructor (GType type,
		 		  guint n_props,
				  GObjectConstructParam *construct_props)
{
  GObject *obj;
  NMWirelessApplet *applet;
  NMWirelessAppletClass *klass;

  klass = NM_WIRELESS_APPLET_CLASS (g_type_class_peek (type));
  obj = G_OBJECT_CLASS (nmwa_parent_class)->constructor (type, n_props, construct_props);
  applet =  NM_WIRELESS_APPLET (obj);

  return obj;
}


void nmwa_about_cb (NMWirelessApplet *applet)
{    
	GdkPixbuf	*pixbuf;
	char		*file;
	GtkWidget	*about_dialog;

	static const gchar *authors[] =
	{
		"\nThe Red Hat Desktop Team, including:\n",
		"Dan Williams <dcbw@redhat.com>",
		"Jonathan Blandford <jrb@redhat.com>",
		"John Palmieri <johnp@redhat.com>",
		"Colin Walters <walters@redhat.com>",
		NULL
	};

	static const gchar *documenters[] =
	{
		NULL
	};

#if (GTK_MAJOR_VERSION <= 2 && GTK_MINOR_VERSION < 6)
	/* GTK 2.4 and earlier, have to use libgnome for about dialog */
	file = gnome_program_locate_file (NULL, GNOME_FILE_DOMAIN_PIXMAP, "gnome-networktool.png", FALSE, NULL);
	pixbuf = gdk_pixbuf_new_from_file (file, NULL);
	g_free (file);

	about_dialog = gnome_about_new (
			_("NetworkManager Applet"),
			VERSION,
			_("(C) 2004-2005 Red Hat, Inc."),
			_("A panel application for managing your network devices & connections."),
			authors,
			documenters,
			NULL,
			pixbuf);
	g_object_unref (pixbuf);

	gtk_window_set_screen (GTK_WINDOW (about_dialog), gtk_widget_get_screen (GTK_WIDGET (applet)));
	g_signal_connect (about_dialog, "destroy", G_CALLBACK (gtk_widget_destroyed), &about_dialog);
	gtk_widget_show (about_dialog);

#else

	/* GTK 2.6 and later code */
	gtk_show_about_dialog (NULL,
		"name",				_("NetworkManager Applet"),
		"version",			VERSION,
		"copyright",			_("(C) 2004-2005 by Red Hat, Inc."),
		"comments",			_("A panel application for managing your network devices & connections."),
		"authors",			authors,
		"documenters",			documenters,
		"translator-credits",	NULL,
		"logo-icon-name",		GTK_STOCK_NETWORK,
		NULL);
#endif
}


/*
 * nmwa_update_network_state
 *
 * Update our state based on what NetworkManager's network state is
 *
 */
static void nmwa_update_network_state (NMWirelessApplet *applet)
{
	static AppletState	old_state = 0;

	g_return_if_fail (applet != NULL);

	if (!applet->connection)
	{
		applet->applet_state = APPLET_STATE_NO_NM;
		goto out;
	}

	if (applet->applet_state == APPLET_STATE_NO_NM)
		goto out;

	if (!applet->nm_status)
	{
		applet->applet_state = APPLET_STATE_NO_CONNECTION;
		goto out;
	}

	if (strcmp (applet->nm_status, "scanning") == 0)
	{
		applet->applet_state = APPLET_STATE_WIRELESS_SCANNING;
		goto out;
	}
	
	if (strcmp (applet->nm_status, "disconnected") == 0)
	{
		applet->applet_state = APPLET_STATE_NO_CONNECTION;
		goto out;
	}
	
	if (!applet->active_device)
	{
		applet->applet_state = APPLET_STATE_NO_CONNECTION;
		goto out;
	}

	/* If the device is not 802.x, we don't show state for it (yet) */
	if (    (applet->active_device->type != DEVICE_TYPE_WIRED_ETHERNET)
		&& (applet->active_device->type != DEVICE_TYPE_WIRELESS_ETHERNET))
	{
		applet->applet_state = APPLET_STATE_NO_CONNECTION;
	}
	else if (applet->active_device->type == DEVICE_TYPE_WIRED_ETHERNET)
	{
		if (strcmp (applet->nm_status, "connecting") == 0)
			applet->applet_state = APPLET_STATE_WIRED_CONNECTING;
		else if (strcmp (applet->nm_status, "connected") == 0)
			applet->applet_state = APPLET_STATE_WIRED;
	}
	else if (applet->active_device->type == DEVICE_TYPE_WIRELESS_ETHERNET)
	{
		if (strcmp (applet->nm_status, "connecting") == 0)
			applet->applet_state = APPLET_STATE_WIRELESS_CONNECTING;
		else if (strcmp (applet->nm_status, "connected") == 0)
			applet->applet_state = APPLET_STATE_WIRELESS;
	}

out:
	if (applet->applet_state != old_state)
	{
		applet->animation_step = 0;
		if (applet->applet_state == APPLET_STATE_NO_NM)
		{
			/* We can only do this because we are called with
			 * the applet->data_mutex locked.
			 */
			g_free (applet->nm_status);
			applet->nm_status = NULL;
		}
		old_state = applet->applet_state;
	}
}


static gboolean
animation_timeout (NMWirelessApplet *applet)
{
	switch (applet->applet_state)
	{
		case (APPLET_STATE_WIRED_CONNECTING):
			if (applet->animation_step >= NUM_WIRED_CONNECTING_FRAMES)
				applet->animation_step = 0;
			gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
					applet->wired_connecting_icons[applet->animation_step]);
			applet->animation_step ++;
			break;

		case (APPLET_STATE_WIRELESS_CONNECTING):
			if (applet->animation_step >= NUM_WIRELESS_CONNECTING_FRAMES)
				applet->animation_step = 0;
			gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
					applet->wireless_connecting_icons[applet->animation_step]);
			applet->animation_step ++;
			break;

		case (APPLET_STATE_NO_NM):
			applet->animation_step = 0;
			break;

		case (APPLET_STATE_WIRELESS_SCANNING):
			if (applet->animation_step >= NUM_WIRELESS_SCANNING_FRAMES)
				applet->animation_step = 0;
			gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap),
					applet->wireless_scanning_icons[applet->animation_step]);
			applet->animation_step ++;
			break;

		default:
			break;
	}
	return TRUE;
}


inline void print_state (AppletState state)
{
	switch (state)
	{
		case (APPLET_STATE_NO_NM):
			g_print ("State: APPLET_STATE_NO_NM\n");
			break;
		case (APPLET_STATE_NO_CONNECTION):
			g_print ("State: APPLET_STATE_NO_CONNECTION\n");
			break;
		case (APPLET_STATE_WIRED):
			g_print ("State: APPLET_STATE_WIRED\n");
			break;
		case (APPLET_STATE_WIRED_CONNECTING):
			g_print ("State: APPLET_STATE_WIRED_CONNECTING\n");
			break;
		case (APPLET_STATE_WIRELESS):
			g_print ("State: APPLET_STATE_WIRELESS\n");
			break;
		case (APPLET_STATE_WIRELESS_CONNECTING):
			g_print ("State: APPLET_STATE_WIRELESS_CONNECTING\n");
			break;
		case (APPLET_STATE_WIRELESS_SCANNING):
			g_print ("State: APPLET_STATE_WIRELESS_SCANNING\n");
			break;
		default:
			g_print ("State: UNKNOWN\n");
			break;
    }
}


/*
 * nmwa_update_state
 *
 * Figure out what the currently active device is from NetworkManager, its type,
 * and what our icon on the panel should look like for each type.
 *
 */
static void
nmwa_update_state (NMWirelessApplet *applet)
{
	gboolean show_applet = TRUE;
	gboolean need_animation = FALSE;
	GdkPixbuf *pixbuf = NULL;
	gint strength = -1;
	char *tip = NULL;
	WirelessNetwork *active_network = NULL;

	g_mutex_lock (applet->data_mutex);
	if (    applet->active_device
		&& (applet->active_device->type == DEVICE_TYPE_WIRELESS_ETHERNET))
	{
		GSList *list;

		/* Grab a pointer the active network (for ESSID) */
		for (list = applet->active_device->networks; list; list = list->next)
		{
			WirelessNetwork *network = (WirelessNetwork *) list->data;

			if (network->active)
				active_network = network;
		}

		strength = CLAMP ((int)applet->active_device->strength, 0, 100);
	}

#if 0
	/* Only show icon if there's more than one device and at least one is wireless */
	if (g_slist_length (applet->device_list) == 1 && applet->applet_state != APPLET_STATE_NO_NM)
	{
		if (((NetworkDevice *)applet->device_list->data)->type == DEVICE_TYPE_WIRED_ETHERNET)
			show_applet = FALSE;
	}
#endif

	nmwa_update_network_state (applet);

	/* print_state (applet->applet_state); */
	switch (applet->applet_state)
	{
		case (APPLET_STATE_NO_CONNECTION):
			show_applet = FALSE;
			tip = g_strdup (_("No network connection"));
			break;

		case (APPLET_STATE_WIRED):
			pixbuf = applet->wired_icon;
			tip = g_strdup (_("Wired network connection"));
			break;

		case (APPLET_STATE_WIRED_CONNECTING):
			need_animation = TRUE;
			tip = g_strdup (_("Connecting to a wired network..."));
			break;

		case (APPLET_STATE_WIRELESS):
			if (applet->active_device)
			{
				if (applet->is_adhoc)
				{
					pixbuf = applet->adhoc_icon;
					tip = g_strdup (_("Connected to an Ad-Hoc wireless network"));
				}
				else
				{
					if (strength > 75)
						pixbuf = applet->wireless_100_icon;
					else if (strength > 50)
						pixbuf = applet->wireless_75_icon;
					else if (strength > 25)
						pixbuf = applet->wireless_50_icon;
					else if (strength > 0)
						pixbuf = applet->wireless_25_icon;
					else
						pixbuf = applet->wireless_00_icon;
					tip = g_strdup_printf (_("Wireless network connection to '%s' (%d%%)"),
							active_network ? active_network->essid : "(unknown)", strength);
				}
			}
			else
				tip = g_strdup (_("Wireless network connection"));
			break;

		case (APPLET_STATE_WIRELESS_CONNECTING):
			need_animation = TRUE;
			tip = g_strdup_printf (_("Connecting to wireless network '%s'..."),
					active_network ? active_network->essid : "(unknown)");
			break;

		case (APPLET_STATE_NO_NM):
			show_applet = FALSE;
			tip = g_strdup (_("NetworkManager is not running"));
			break;

		case (APPLET_STATE_WIRELESS_SCANNING):
			need_animation = TRUE;
			tip = g_strdup (_("Scanning for wireless networks..."));
			break;

		default:
			break;
	}
	g_mutex_unlock (applet->data_mutex);

	if (!applet->tooltips)
		applet->tooltips = gtk_tooltips_new ();
	gtk_tooltips_set_tip (applet->tooltips, applet->event_box, tip, NULL);
	g_free (tip);

	/*determine if we should hide the notification icon*/
	if (show_applet)
		gtk_widget_show (GTK_WIDGET (applet));
	else
		gtk_widget_hide (GTK_WIDGET (applet));

	if (applet->animation_id)
		g_source_remove (applet->animation_id);
	if (need_animation)
		applet->animation_id = g_timeout_add (100, (GSourceFunc) (animation_timeout), applet);
	else
		gtk_image_set_from_pixbuf (GTK_IMAGE (applet->pixmap), pixbuf);
}


/*
 * nmwa_redraw_timeout
 *
 * Called regularly to update the applet's state and icon in the panel
 *
 */
static int nmwa_redraw_timeout (NMWirelessApplet *applet)
{
	nmwa_update_state (applet);

  	return (TRUE);
}

static void nmwa_start_redraw_timeout (NMWirelessApplet *applet)
{
	applet->redraw_timeout_id =
		g_timeout_add (CFG_UPDATE_INTERVAL * 1000, (GtkFunction) nmwa_redraw_timeout, applet);
}


/*
 * show_warning_dialog
 *
 * pop up a warning or error dialog with certain text
 *
 */
void show_warning_dialog (gboolean error, gchar *mesg, ...)
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
 * nmwa_destroy
 *
 * Destroy the applet and clean up its data
 *
 */
static void nmwa_destroy (NMWirelessApplet *applet, gpointer user_data)
{
	if (applet->menu)
		nmwa_dispose_menu_items (applet);

	if (applet->redraw_timeout_id > 0)
	{
		gtk_timeout_remove (applet->redraw_timeout_id);
		applet->redraw_timeout_id = 0;
	}

	if (applet->gconf_client)
		g_object_unref (G_OBJECT (applet->gconf_client));
}


/*
 * nmwa_update_network_timestamp
 *
 * Update the timestamp of a network in GConf.
 *
 */
static void nmwa_update_network_timestamp (NMWirelessApplet *applet, const WirelessNetwork *network)
{
	char		*key;
	char		*escaped_network;

	g_return_if_fail (applet != NULL);
	g_return_if_fail (network != NULL);

	/* Update GConf to set timestamp for this network, or add it if
	 * it doesn't already exist.
	 */

	/* Update timestamp on network */
	escaped_network = gconf_escape_key (network->essid, strlen (network->essid));
	key = g_strdup_printf ("%s/%s/timestamp", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	gconf_client_set_int (applet->gconf_client, key, time (NULL), NULL);
	g_free (key);

	/* Force-set the essid too so that we have a semi-complete network entry */
	key = g_strdup_printf ("%s/%s/essid", NMI_GCONF_WIRELESS_NETWORKS_PATH, escaped_network);
	gconf_client_set_string (applet->gconf_client, key, network->essid, NULL);
	g_free (key);
	g_free (escaped_network);
}


/*
 * nmwa_get_device_network_for_essid
 *
 * Searches the network list for a given network device and returns the
 * Wireless Network structure corresponding to it.
 *
 */
WirelessNetwork *nmwa_get_device_network_for_essid (NMWirelessApplet *applet, NetworkDevice *dev, const char *essid)
{
	WirelessNetwork	*found_network = NULL;
	GSList			*element;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (dev != NULL, NULL);
	g_return_val_if_fail (essid != NULL, NULL);
	g_return_val_if_fail (strlen (essid), NULL);

	g_mutex_lock (applet->data_mutex);
	element = dev->networks;
	while (element)
	{
		WirelessNetwork	*network = (WirelessNetwork *)(element->data);
		if (network && (strcmp (network->essid, essid) == 0))
		{
			found_network = network;
			break;
		}
		element = g_slist_next (element);
	}
	g_mutex_unlock (applet->data_mutex);

	return (found_network);
}


/*
 * nmwa_get_device_for_nm_device
 *
 * Searches the device list for a device that matches the
 * NetworkManager ID given.
 *
 */
NetworkDevice *nmwa_get_device_for_nm_device (NMWirelessApplet *applet, const char *nm_dev)
{
	NetworkDevice	*found_dev = NULL;
	GSList		*element;

	g_return_val_if_fail (applet != NULL, NULL);
	g_return_val_if_fail (nm_dev != NULL, NULL);
	g_return_val_if_fail (strlen (nm_dev), NULL);

	g_mutex_lock (applet->data_mutex);
	element = applet->device_list;
	while (element)
	{
		NetworkDevice	*dev = (NetworkDevice *)(element->data);
		if (dev && (strcmp (dev->nm_device, nm_dev) == 0))
		{
			found_dev = dev;
			break;
		}
		element = g_slist_next (element);
	}
	g_mutex_unlock (applet->data_mutex);

	return (found_dev);
}


/*
 * nmwa_menu_item_activate
 *
 * Signal function called when user clicks on a menu item
 *
 */
static void nmwa_menu_item_activate (GtkMenuItem *item, gpointer user_data)
{
	NMWirelessApplet	*applet = (NMWirelessApplet *)user_data;
	NetworkDevice		*dev = NULL;
	WirelessNetwork	*net = NULL;
	char				*tag;

	g_return_if_fail (item != NULL);
	g_return_if_fail (applet != NULL);

	if ((tag = g_object_get_data (G_OBJECT (item), "network")))
	{
		char	*item_dev = g_object_get_data (G_OBJECT (item), "nm_device");

		if (item_dev && (dev = nmwa_get_device_for_nm_device (applet, item_dev)))
			if ((net = nmwa_get_device_network_for_essid (applet, dev, tag)))
				nmwa_update_network_timestamp (applet, net);
	}
	else if ((tag = g_object_get_data (G_OBJECT (item), "device")))
		dev = nmwa_get_device_for_nm_device (applet, tag);

	if (dev)
		nmwa_dbus_set_device (applet->connection, dev, net, -1, NULL);
}


/*
 * nmwa_toplevel_menu_activate
 *
 * Pop up the wireless networks menu in response to a click on the applet
 *
 */
static void nmwa_toplevel_menu_activate (GtkWidget *menu, NMWirelessApplet *applet)
{
	if (!applet->tooltips)
		applet->tooltips = gtk_tooltips_new ();
	gtk_tooltips_set_tip (applet->tooltips, applet->event_box, NULL, NULL);

	nmwa_dispose_menu_items (applet);
	nmwa_populate_menu (applet);
	gtk_widget_show (applet->menu);
}


/*
 * nmwa_menu_add_separator_item
 *
 */
static void nmwa_menu_add_separator_item (GtkWidget *menu)
{
	GtkWidget	*menu_item;
	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nmwa_menu_add_text_item
 *
 * Add a non-clickable text item to a menu
 *
 */
static void nmwa_menu_add_text_item (GtkWidget *menu, char *text)
{
	GtkWidget		*menu_item;

	g_return_if_fail (text != NULL);
	g_return_if_fail (menu != NULL);

	menu_item = gtk_menu_item_new_with_label (text);
	gtk_widget_set_sensitive (menu_item, FALSE);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


/*
 * nmwa_menu_add_device_item
 *
 * Add a network device to the menu
 *
 */
static void nmwa_menu_add_device_item (GtkWidget *menu, NetworkDevice *device, gboolean current, gint n_devices, NMWirelessApplet *applet)
{
	GtkWidget		*menu_item;

	g_return_if_fail (menu != NULL);

	if (device->type == DEVICE_TYPE_WIRED_ETHERNET)
	{
	     menu_item = nm_menu_wired_new ();
	     nm_menu_wired_update (NM_MENU_WIRED (menu_item), device, n_devices);
		if (applet->active_device == device)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
	}
	else
	{
	     menu_item = nm_menu_network_new ();
	     nm_menu_network_update (NM_MENU_NETWORK (menu_item), device, n_devices);
	}

	g_object_set_data (G_OBJECT (menu_item), "device", g_strdup (device->nm_device));
	g_signal_connect(G_OBJECT (menu_item), "activate", G_CALLBACK(nmwa_menu_item_activate), applet);

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_show (menu_item);
}


static void custom_essid_item_selected (GtkWidget *menu_item, NMWirelessApplet *applet)
{
	nmwa_other_network_dialog_run (applet, FALSE);
}


static void nmwa_menu_add_custom_essid_item (GtkWidget *menu, NMWirelessApplet *applet)
{
  GtkWidget *menu_item;
  GtkWidget *label;

  menu_item = gtk_menu_item_new ();
  label = gtk_label_new (_("Other Wireless Networks..."));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_container_add (GTK_CONTAINER (menu_item), label);
  gtk_widget_show_all (menu_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
  g_signal_connect (menu_item, "activate", G_CALLBACK (custom_essid_item_selected), applet);
}


static void new_network_item_selected (GtkWidget *menu_item, NMWirelessApplet *applet)
{
	nmwa_other_network_dialog_run (applet, TRUE);
}


static void nmwa_menu_add_create_network_item (GtkWidget *menu, NMWirelessApplet *applet)
{
  GtkWidget *menu_item;
  GtkWidget *label;

  menu_item = gtk_menu_item_new ();
  label = gtk_label_new (_("Create new Wireless Network..."));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_container_add (GTK_CONTAINER (menu_item), label);
  gtk_widget_show_all (menu_item);
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
  g_signal_connect (menu_item, "activate", G_CALLBACK (new_network_item_selected), applet);
}

/*
 * nmwa_menu_device_add_networks
 *
 */
static void nmwa_menu_device_add_networks (GtkWidget *menu, NetworkDevice *dev, NMWirelessApplet *applet)
{
	GSList *list;
	gboolean has_encrypted = FALSE;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);
	g_return_if_fail (dev != NULL);

	if (dev->type != DEVICE_TYPE_WIRELESS_ETHERNET)
		return;

	/* Check for any security */
	for (list = dev->networks; list; list = list->next)
	{
		WirelessNetwork *network = list->data;

		if (network->encrypted)
			has_encrypted = TRUE;
	}

	/* Add all networks in our network list to the menu */
	for (list = dev->networks; list; list = list->next)
	{
		GtkWidget *menu_item;
		WirelessNetwork *net;

		net = (WirelessNetwork *) list->data;

		menu_item = nm_menu_wireless_new (applet->encryption_size_group);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		if (applet->active_device == dev && net->active)
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menu_item), TRUE);
		nm_menu_wireless_update (NM_MENU_WIRELESS (menu_item), net, has_encrypted);

		g_object_set_data (G_OBJECT (menu_item), "network", g_strdup (net->essid));
		g_object_set_data (G_OBJECT (menu_item), "nm_device", g_strdup (dev->nm_device));
		g_signal_connect(G_OBJECT (menu_item), "activate", G_CALLBACK (nmwa_menu_item_activate), applet);

		gtk_widget_show (menu_item);
	}
}

static int
sort_networks_function (gconstpointer a, gconstpointer b)
{
	NetworkDevice *dev_a = (NetworkDevice *) a;
	NetworkDevice *dev_b = (NetworkDevice *) b;
	char *name_a;
	char *name_b;

	if (dev_a->hal_name)
		name_a = dev_a->hal_name;
	else if (dev_a->nm_name)
		name_a = dev_a->nm_name;
	else
		name_a = "";

	if (dev_b->hal_name)
		name_b = dev_b->hal_name;
	else if (dev_b->nm_name)
		name_b = dev_b->nm_name;
	else
		name_b = "";

	if (dev_a->type == dev_b->type)
	{
		return strcmp (name_a, name_b);
	}
	if (dev_a->type == DEVICE_TYPE_WIRED_ETHERNET)
		return -1;
	if (dev_b->type == DEVICE_TYPE_WIRED_ETHERNET)
		return 1;
	if (dev_a->type == DEVICE_TYPE_WIRELESS_ETHERNET)
		return -1;
	if (dev_b->type == DEVICE_TYPE_WIRELESS_ETHERNET)
		return 1;

	/* Unknown device types.  Sort by name only at this point. */
	return strcmp (name_a, name_b);
}

/*
 * nmwa_menu_add_devices
 *
 */
static void nmwa_menu_add_devices (GtkWidget *menu, NMWirelessApplet *applet)
{
	GSList	*element;
	gint n_wireless_interfaces = 0;
	gint n_wired_interfaces = 0;

	g_return_if_fail (menu != NULL);
	g_return_if_fail (applet != NULL);

	g_mutex_lock (applet->data_mutex);
	if (! applet->device_list)
	{
		nmwa_menu_add_text_item (menu, _("No network devices have been found"));
		g_mutex_unlock (applet->data_mutex);
		return;
	}

	applet->device_list = g_slist_sort (applet->device_list, sort_networks_function);

	for (element = applet->device_list; element; element = element->next)
	{
		NetworkDevice *dev = (NetworkDevice *)(element->data);

		g_assert (dev);

		switch (dev->type)
		{
		case DEVICE_TYPE_WIRELESS_ETHERNET:
			n_wireless_interfaces++;
			break;
		case DEVICE_TYPE_WIRED_ETHERNET:
			n_wired_interfaces++;
			break;
		default:
			break;
		}
	}

	/* Add all devices in our device list to the menu */
	for (element = applet->device_list; element; element = element->next)
	{
		NetworkDevice *dev = (NetworkDevice *)(element->data);

		if (dev && ((dev->type == DEVICE_TYPE_WIRED_ETHERNET) || (dev->type == DEVICE_TYPE_WIRELESS_ETHERNET)))
		{
			gboolean current = (dev == applet->active_device);
			gint n_devices = 0;

			if (dev->type == DEVICE_TYPE_WIRED_ETHERNET)
				n_devices = n_wired_interfaces;
			else if (dev->type == DEVICE_TYPE_WIRELESS_ETHERNET)
				n_devices = n_wireless_interfaces;

			nmwa_menu_add_device_item (menu, dev, current, n_devices, applet);
			nmwa_menu_device_add_networks (menu, dev, applet);
		}
	}

	if (n_wireless_interfaces > 0)
	{
		/* Add the "Other wireless network..." entry */
		nmwa_menu_add_separator_item (menu);
		nmwa_menu_add_custom_essid_item (menu, applet);
		nmwa_menu_add_create_network_item (menu, applet);
	}

	g_mutex_unlock (applet->data_mutex);
}


/*
 * nmwa_menu_item_data_free
 *
 * Frees the "network" data tag on a menu item we've created
 *
 */
static void nmwa_menu_item_data_free (GtkWidget *menu_item, gpointer data)
{
	char	*tag;
	GtkWidget *menu;

	g_return_if_fail (menu_item != NULL);

	menu = GTK_WIDGET(data);

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "network")))
	{
		g_object_set_data (G_OBJECT (menu_item), "network", NULL);
		g_free (tag);
	}

	if ((tag = g_object_get_data (G_OBJECT (menu_item), "nm_device")))
	{
		g_object_set_data (G_OBJECT (menu_item), "nm_device", NULL);
		g_free (tag);
	}

	gtk_container_remove(GTK_CONTAINER(menu), menu_item);
}


/*
 * nmwa_dispose_menu_items
 *
 * Destroy the menu and each of its items data tags
 *
 */
static void nmwa_dispose_menu_items (NMWirelessApplet *applet)
{
	g_return_if_fail (applet != NULL);

	/* Free the "network" data on each menu item */
	gtk_container_foreach (GTK_CONTAINER (applet->menu), nmwa_menu_item_data_free, applet->menu);
}


/*
 * nmwa_populate_menu
 *
 * Set up our networks menu from scratch
 *
 */
static GtkWidget * nmwa_populate_menu (NMWirelessApplet *applet)
{
	GtkWidget		 *menu = applet->menu;

	g_return_val_if_fail (applet != NULL, NULL);

	if (applet->applet_state == APPLET_STATE_NO_NM)
	{
		nmwa_menu_add_text_item (menu, _("NetworkManager is not running..."));
		return NULL;
	}

	nmwa_menu_add_devices (menu, applet);

	return (menu);
}


static void nmwa_set_scanning_enabled_cb (GtkWidget *widget, NMWirelessApplet *applet)
{
	g_return_if_fail (applet != NULL);

	nmwa_dbus_enable_scanning (applet, !applet->scanning_enabled);
}

static void nmwa_set_wireless_enabled_cb (GtkWidget *widget, NMWirelessApplet *applet)
{
	g_return_if_fail (applet != NULL);

	nmwa_dbus_enable_scanning (applet, !applet->wireless_enabled);
}


/*
 * nmwa_context_menu_update
 *
 */
static void nmwa_context_menu_update (NMWirelessApplet *applet)
{
	GtkWidget *image;	

	g_return_if_fail (applet != NULL);
	g_return_if_fail (applet->pause_scanning_item != NULL);
	g_return_if_fail (applet->stop_wireless_item != NULL);

	g_mutex_lock (applet->data_mutex);

	gtk_widget_destroy (applet->pause_scanning_item);
	gtk_widget_destroy (applet->stop_wireless_item);

	if (applet->scanning_enabled)
	{
		applet->pause_scanning_item = gtk_image_menu_item_new_with_label (_("Pause Wireless Scanning"));
		image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_MENU);
	}
	else
	{
		applet->pause_scanning_item = gtk_image_menu_item_new_with_label (_("Resume Wireless Scanning"));
		image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PLAY, GTK_ICON_SIZE_MENU);
	}
	g_signal_connect (G_OBJECT (applet->pause_scanning_item), "activate", G_CALLBACK (nmwa_set_scanning_enabled_cb), applet);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->pause_scanning_item), image);
	gtk_menu_shell_insert (GTK_MENU_SHELL (applet->context_menu), applet->pause_scanning_item, 0);
	gtk_widget_show_all (applet->pause_scanning_item);

	if (applet->wireless_enabled)
	{
		applet->stop_wireless_item = gtk_image_menu_item_new_with_label (_("Stop All Wireless Devices"));
		image = gtk_image_new_from_stock (GTK_STOCK_STOP, GTK_ICON_SIZE_MENU);
	}
	else
	{
		applet->stop_wireless_item = gtk_image_menu_item_new_with_label (_("Start All Wireless Devices"));
		image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PLAY, GTK_ICON_SIZE_MENU);
	}
	g_signal_connect (G_OBJECT (applet->stop_wireless_item), "activate", G_CALLBACK (nmwa_set_wireless_enabled_cb), applet);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->stop_wireless_item), image);
	gtk_menu_shell_insert (GTK_MENU_SHELL (applet->context_menu), applet->stop_wireless_item, 1);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->stop_wireless_item), FALSE);
	gtk_widget_show_all (applet->stop_wireless_item);

	g_mutex_unlock (applet->data_mutex);
}


/*
 * nmwa_context_menu_create
 *
 * Generate the contextual popup menu.
 *
 */
static GtkWidget *nmwa_context_menu_create (NMWirelessApplet *applet)
{
	GtkWidget	*menu;
	GtkWidget	*menu_item;
	GtkWidget *image;
	
	g_return_val_if_fail (applet != NULL, NULL);

	menu = gtk_menu_new ();

	applet->pause_scanning_item = gtk_image_menu_item_new_with_label (_("Pause Wireless Scanning"));
	g_signal_connect (G_OBJECT (applet->pause_scanning_item), "activate", G_CALLBACK (nmwa_set_scanning_enabled_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_MEDIA_PAUSE, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->pause_scanning_item), image);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), applet->pause_scanning_item);

	applet->stop_wireless_item = gtk_image_menu_item_new_with_label (_("Stop All Wireless Devices"));
	g_signal_connect (G_OBJECT (applet->stop_wireless_item), "activate", G_CALLBACK (nmwa_set_wireless_enabled_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_STOP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (applet->stop_wireless_item), image);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), applet->stop_wireless_item);
	gtk_widget_set_sensitive (GTK_WIDGET (applet->stop_wireless_item), FALSE);

	menu_item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	menu_item = gtk_image_menu_item_new_with_label (_("Help"));
/*	g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (nmwa_help_cb), applet); */
	image = gtk_image_new_from_stock (GTK_STOCK_HELP, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
	gtk_widget_set_sensitive (GTK_WIDGET (menu_item), FALSE);

	menu_item = gtk_image_menu_item_new_with_label (_("About"));
	g_signal_connect (G_OBJECT (menu_item), "activate", G_CALLBACK (nmwa_about_cb), applet);
	image = gtk_image_new_from_stock (GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menu_item), image);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);

	gtk_widget_show_all (menu);

	return menu;
}


/*
 * nmwa_setup_widgets
 *
 * Intialize the applet's widgets and packing, create the initial
 * menu of networks.
 *
 */
static void nmwa_setup_widgets (NMWirelessApplet *applet)
{
	GtkWidget      *menu_bar;
	GtkWidget		*event_box;

	/* construct pixmap widget */
	applet->pixmap = gtk_image_new ();
	applet->event_box = gtk_event_box_new ();
	gtk_container_set_border_width (GTK_CONTAINER (applet->event_box), 0);

	menu_bar = gtk_menu_bar_new ();
	gtk_container_add (GTK_CONTAINER(applet->event_box), menu_bar);

	applet->toplevel_menu = gtk_menu_item_new();
	gtk_widget_set_name (applet->toplevel_menu, "ToplevelMenu");
	gtk_container_set_border_width (GTK_CONTAINER (applet->toplevel_menu), 0);
	gtk_container_add (GTK_CONTAINER(applet->toplevel_menu), applet->pixmap);
	gtk_menu_shell_append (GTK_MENU_SHELL(menu_bar), applet->toplevel_menu);
	g_signal_connect (applet->toplevel_menu, "activate", G_CALLBACK (nmwa_toplevel_menu_activate), applet);

	applet->context_menu = nmwa_context_menu_create (applet);
	g_signal_connect (applet->toplevel_menu, "button_press_event", G_CALLBACK (nmwa_toplevel_menu_button_press), applet);

	applet->menu = gtk_menu_new();
	gtk_menu_item_set_submenu (GTK_MENU_ITEM(applet->toplevel_menu), applet->menu);

	applet->encryption_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_container_add (GTK_CONTAINER (applet), applet->event_box);

	gtk_widget_show_all (GTK_WIDGET (applet));
}


/*
 * nmwa_toplevel_menu_button_press
 *
 * Handle right-clicks for the context popup menu
 *
 */
static gboolean nmwa_toplevel_menu_button_press (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	NMWirelessApplet	*applet = (NMWirelessApplet *)user_data;

	g_return_val_if_fail (applet != NULL, FALSE);

	if (event->button != 1)
		g_signal_stop_emission_by_name (widget, "button_press_event");

	if (event->button == 3)
	{
		nmwa_context_menu_update (applet);
		gtk_menu_popup (GTK_MENU (applet->context_menu), NULL, NULL, NULL, applet, event->button, event->time);
		return (TRUE);
	}

	return (FALSE);
}

/*
 * nmwa_get_instance
 *
 * Create the initial instance of our wireless applet
 *
 */
static GtkWidget * nmwa_get_instance (NMWirelessApplet *applet)
{
	GError	*error = NULL;

	gtk_widget_hide(GTK_WIDGET(applet));

	applet->gconf_client = gconf_client_get_default ();
	if (!applet->gconf_client)
		return (NULL);

	applet->applet_state = APPLET_STATE_NO_NM;
	applet->device_list = NULL;
	applet->active_device = NULL;
	applet->nm_status = NULL;
	applet->tooltips = NULL;

	/* Start our dbus thread */
	if (!(applet->data_mutex = g_mutex_new ()))
	{
		g_object_unref (G_OBJECT (applet->gconf_client));
		return (NULL);
	}
	if (!(applet->dbus_thread = g_thread_create (nmwa_dbus_worker, applet, FALSE, &error)))
	{
		g_mutex_free (applet->data_mutex);
		g_object_unref (G_OBJECT (applet->gconf_client));
		return (NULL);
	}

	/* Load pixmaps and create applet widgets */
	nmwa_setup_widgets (applet);

	g_signal_connect (applet,"destroy", G_CALLBACK (nmwa_destroy), NULL);

	/* Start redraw timeout */
	nmwa_start_redraw_timeout (applet);

	return (GTK_WIDGET (applet));
}

static gboolean nmwa_fill (NMWirelessApplet *applet)
{
/*	gtk_window_set_default_icon_from_file (ICONDIR"/NMWirelessApplet/wireless-applet.png", NULL); */

	glade_gnome_init ();
	gtk_widget_show (nmwa_get_instance (applet));
	return (TRUE);
}

static void
setup_stock (void)
{
	GtkIconFactory *ifactory;
	GtkIconSet *iset;
	GtkIconSource *isource;
	static gboolean initted = FALSE;

	if (initted)
		return;

	ifactory = gtk_icon_factory_new ();
	iset = gtk_icon_set_new ();
	isource = gtk_icon_source_new ();

	/* we use the lockscreen icon to get a key */
	gtk_icon_source_set_icon_name (isource, "gnome-lockscreen");
	gtk_icon_set_add_source (iset, isource);
	gtk_icon_factory_add (ifactory, "gnome-lockscreen", iset);
	gtk_icon_factory_add_default (ifactory);

	initted = TRUE;
}

static void
nmwa_icons_free (NMWirelessApplet *applet)
{
	gint i;

        g_object_unref (applet->no_nm_icon);
        g_object_unref (applet->wired_icon);
        g_object_unref (applet->adhoc_icon);
	for (i = 0; i < NUM_WIRED_CONNECTING_FRAMES; i++)
		g_object_unref (applet->wired_connecting_icons[i]);
        g_object_unref (applet->wireless_00_icon);
        g_object_unref (applet->wireless_25_icon);
        g_object_unref (applet->wireless_50_icon);
        g_object_unref (applet->wireless_75_icon);
        g_object_unref (applet->wireless_100_icon);
	for (i = 0; i < NUM_WIRELESS_CONNECTING_FRAMES; i++)
		g_object_unref (applet->wireless_connecting_icons[i]);
	for (i = 0; i < NUM_WIRELESS_SCANNING_FRAMES; i++)
		g_object_unref (applet->wireless_scanning_icons[i]);
}

static void
nmwa_icons_load_from_disk (NMWirelessApplet *applet, GtkIconTheme *icon_theme)
{
	/* Assume icons are square */
	gint icon_size = 22;

	applet->no_nm_icon = gtk_icon_theme_load_icon (icon_theme, "nm-device-broken", icon_size, 0, NULL);
	applet->wired_icon = gtk_icon_theme_load_icon (icon_theme, "nm-device-wired", icon_size, 0, NULL);
	applet->adhoc_icon = gtk_icon_theme_load_icon (icon_theme, "nm-adhoc", icon_size, 0, NULL);
	applet->wired_connecting_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting01", icon_size, 0, NULL);
	applet->wired_connecting_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting02", icon_size, 0, NULL);
	applet->wired_connecting_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting03", icon_size, 0, NULL);
	applet->wired_connecting_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting04", icon_size, 0, NULL);
	applet->wired_connecting_icons[4] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting05", icon_size, 0, NULL);
	applet->wired_connecting_icons[5] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting06", icon_size, 0, NULL);
	applet->wired_connecting_icons[6] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting07", icon_size, 0, NULL);
	applet->wired_connecting_icons[7] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting08", icon_size, 0, NULL);
	applet->wired_connecting_icons[8] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting09", icon_size, 0, NULL);
	applet->wired_connecting_icons[9] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting10", icon_size, 0, NULL);
	applet->wired_connecting_icons[10] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting11", icon_size, 0, NULL);
	applet->wireless_00_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-00", icon_size, 0, NULL);
	applet->wireless_25_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-25", icon_size, 0, NULL);
	applet->wireless_50_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-50", icon_size, 0, NULL);
	applet->wireless_75_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-75", icon_size, 0, NULL);
	applet->wireless_100_icon = gtk_icon_theme_load_icon (icon_theme, "nm-signal-100", icon_size, 0, NULL);
	applet->wireless_connecting_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting01", icon_size, 0, NULL);
	applet->wireless_connecting_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting02", icon_size, 0, NULL);
	applet->wireless_connecting_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting03", icon_size, 0, NULL);
	applet->wireless_connecting_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting04", icon_size, 0, NULL);
	applet->wireless_connecting_icons[4] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting05", icon_size, 0, NULL);
	applet->wireless_connecting_icons[5] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting06", icon_size, 0, NULL);
	applet->wireless_connecting_icons[6] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting07", icon_size, 0, NULL);
	applet->wireless_connecting_icons[7] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting08", icon_size, 0, NULL);
	applet->wireless_connecting_icons[8] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting09", icon_size, 0, NULL);
	applet->wireless_connecting_icons[9] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting10", icon_size, 0, NULL);
	applet->wireless_connecting_icons[10] = gtk_icon_theme_load_icon (icon_theme, "nm-connecting11", icon_size, 0, NULL);
	applet->wireless_scanning_icons[0] = gtk_icon_theme_load_icon (icon_theme, "nm-detect01", icon_size, 0, NULL);
	applet->wireless_scanning_icons[1] = gtk_icon_theme_load_icon (icon_theme, "nm-detect02", icon_size, 0, NULL);
	applet->wireless_scanning_icons[2] = gtk_icon_theme_load_icon (icon_theme, "nm-detect03", icon_size, 0, NULL);
	applet->wireless_scanning_icons[3] = gtk_icon_theme_load_icon (icon_theme, "nm-detect04", icon_size, 0, NULL);
	applet->wireless_scanning_icons[4] = gtk_icon_theme_load_icon (icon_theme, "nm-detect05", icon_size, 0, NULL);
	applet->wireless_scanning_icons[5] = gtk_icon_theme_load_icon (icon_theme, "nm-detect06", icon_size, 0, NULL);
	applet->wireless_scanning_icons[6] = gtk_icon_theme_load_icon (icon_theme, "nm-detect07", icon_size, 0, NULL);
	applet->wireless_scanning_icons[7] = gtk_icon_theme_load_icon (icon_theme, "nm-detect08", icon_size, 0, NULL);
	applet->wireless_scanning_icons[8] = gtk_icon_theme_load_icon (icon_theme, "nm-detect09", icon_size, 0, NULL);
	applet->wireless_scanning_icons[9] = gtk_icon_theme_load_icon (icon_theme, "nm-detect10", icon_size, 0, NULL);
	applet->wireless_scanning_icons[10] = gtk_icon_theme_load_icon (icon_theme, "nm-detect11", icon_size, 0, NULL);
	applet->wireless_scanning_icons[11] = gtk_icon_theme_load_icon (icon_theme, "nm-detect12", icon_size, 0, NULL);
	applet->wireless_scanning_icons[12] = gtk_icon_theme_load_icon (icon_theme, "nm-detect13", icon_size, 0, NULL);
	applet->wireless_scanning_icons[13] = gtk_icon_theme_load_icon (icon_theme, "nm-detect14", icon_size, 0, NULL);
	applet->wireless_scanning_icons[14] = gtk_icon_theme_load_icon (icon_theme, "nm-detect15", icon_size, 0, NULL);
	applet->wireless_scanning_icons[15] = gtk_icon_theme_load_icon (icon_theme, "nm-detect16", icon_size, 0, NULL);
}

static void
nmwa_icon_theme_changed (GtkIconTheme     *icon_theme,
			 NMWirelessApplet *applet)
{
	nmwa_icons_free (applet);
	nmwa_icons_load_from_disk (applet, icon_theme);
	/* FIXME: force redraw */
}
const gchar *style = " \
style \"MenuBar\" \
{ \
  GtkMenuBar::shadow_type = GTK_SHADOW_NONE \
  GtkMenuBar::internal-padding = 0 \
} \
style \"MenuItem\" \
{ \
  xthickness=0 \
  ythickness=0 \
} \
class \"GtkMenuBar\" style \"MenuBar\"\
widget \"*ToplevelMenu*\" style \"MenuItem\"\
";

static void 
nmwa_icons_init (NMWirelessApplet *applet)
{
	GtkIconTheme *icon_theme;

	/* FIXME: Do we need to worry about other screens? */
	gtk_rc_parse_string (style);

	icon_theme = gtk_icon_theme_get_default ();
	nmwa_icons_load_from_disk (applet, icon_theme);
	g_signal_connect (icon_theme, "changed", G_CALLBACK (nmwa_icon_theme_changed), applet);
}


NMWirelessApplet *
nmwa_new ()
{
	return g_object_new (NM_TYPE_WIRELESS_APPLET, "title", "NetworkManager", NULL);
}

