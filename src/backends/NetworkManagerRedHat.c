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
 * (C) Copyright 2004 Red Hat, Inc.
 */

#include <stdio.h>
#include <sys/types.h>
#include <signal.h>
#include <arpa/inet.h>
#include "NetworkManagerSystem.h"
#include "NetworkManagerUtils.h"
#include "NetworkManagerDevice.h"
#include "shvar.h"

/*
 * nm_system_init
 *
 * Initializes the distribution-specific system backend
 *
 */
void nm_system_init (void)
{
}


/*
 * nm_system_device_run_dhcp
 *
 * Run the dhcp daemon for a particular interface.
 *
 * Returns:	TRUE on success
 *			FALSE on dhcp error
 *
 */
gboolean nm_system_device_run_dhcp (NMDevice *dev)
{
	char			*buf;
	const char	*iface;
	int			 err;

	g_return_val_if_fail (dev != NULL, FALSE);

	/* Fake it for a test device */
	if (nm_device_is_test_device (dev))
	{
		g_usleep (2000);
		return (TRUE);
	}

	/* Unfortunately, dhclient can take a long time to get a dhcp address
	 * (for example, bad WEP key so it can't actually talk to the AP).
	 */
	iface = nm_device_get_iface (dev);
	buf = g_strdup_printf ("/sbin/dhclient -1 -q -lf /var/lib/dhcp/dhclient-%s.leases -pf /var/run/dhclient-%s.pid -cf /etc/dhclient-%s.conf %s\n", iface, iface, iface, iface);
	err = nm_spawn_process (buf);
	g_free (buf);
	return (err == 0);
}


/*
 * nm_system_device_stop_dhcp
 *
 * Kill any dhcp daemon that happens to be around.  We may be changing
 * interfaces and we're going to bring the previous one down, so there's
 * no sense in keeping the dhcp daemon running on the old interface.
 *
 */
void nm_system_device_stop_dhcp (NMDevice *dev)
{
	FILE			*pidfile;
	char			*buf;

	g_return_if_fail (dev != NULL);

	/* Not really applicable for test devices */
	if (nm_device_is_test_device (dev))
		return;

	/* Find and kill the previous dhclient process for this device */
	buf = g_strdup_printf ("/var/run/dhclient-%s.pid", nm_device_get_iface (dev));
	pidfile = fopen (buf, "r");
	if (pidfile)
	{
		int			len;
		unsigned char	s_pid[20];
		pid_t		n_pid = -1;

		memset (s_pid, 0, 20);
		fgets (s_pid, 20, pidfile);
		len = strlen (s_pid);
		fclose (pidfile);

		n_pid = atoi (s_pid);
		if (n_pid > 0)
			kill (n_pid, SIGKILL);
	}
	g_free (buf);
}


/*
 * nm_system_device_flush_routes
 *
 * Flush all routes associated with a network device
 *
 */
void nm_system_device_flush_routes (NMDevice *dev)
{
	char	*buf;

	g_return_if_fail (dev != NULL);

	/* Not really applicable for test devices */
	if (nm_device_is_test_device (dev))
		return;

	/* Remove routing table entries */
	buf = g_strdup_printf ("/sbin/ip route flush dev %s", nm_device_get_iface (dev));
	nm_spawn_process (buf);
	g_free (buf);
}


/*
 * nm_system_device_has_active_routes
 *
 * Find out whether the specified device has any routes in the routing
 * table.
 *
 */
gboolean nm_system_device_has_active_routes (NMDevice *dev)
{
	return (FALSE);
}


/*
 * nm_system_device_flush_addresses
 *
 * Flush all network addresses associated with a network device
 *
 */
void nm_system_device_flush_addresses (NMDevice *dev)
{
	char	*buf;

	g_return_if_fail (dev != NULL);

	/* Not really applicable for test devices */
	if (nm_device_is_test_device (dev))
		return;

	/* Remove all IP addresses for a device */
	buf = g_strdup_printf ("/sbin/ip address flush dev %s", nm_device_get_iface (dev));
	nm_spawn_process (buf);
	g_free (buf);
}


/*
 * nm_system_device_setup_static_ip4_config
 *
 * Set up the device with a particular IPv4 address/netmask/gateway.
 *
 * Returns:	TRUE	on success
 *			FALSE on error
 *
 */
gboolean nm_system_device_setup_static_ip4_config (NMDevice *dev)
{
#define IPBITS	(sizeof (guint32) * 8)
	struct in_addr	 temp_addr;
	struct in_addr  temp_addr2;
	char			*s_tmp;
	char			*s_tmp2;
	int			 i;
	guint32		 addr;
	guint32		 netmask;
	guint32		 prefix = IPBITS;	/* initialize with # bits in ip4 address */
	guint32		 broadcast;
	char			*buf;
	int			 err;
	const char	*iface;

	g_return_val_if_fail (dev != NULL, FALSE);
	g_return_val_if_fail (!nm_device_config_get_use_dhcp (dev), FALSE);

	addr = nm_device_config_get_ip4_address (dev);
	netmask = nm_device_config_get_ip4_netmask (dev);
	iface = nm_device_get_iface (dev);
	broadcast = nm_device_config_get_ip4_broadcast (dev);

	/* Calculate the prefix (# bits stripped off by the netmask) */
	for (i = 0; i < IPBITS; i++)
	{
		if (!(ntohl (netmask) & ((2 << i) - 1)))
			prefix--;
	}

	/* Calculate the broadcast address if the user didn't specify one */
	if (!broadcast)
		broadcast = ((addr & (int)netmask) | ~(int)netmask);

	/* FIXME: what if some other device is already using our IP address? */

	/* Set our IP address */
	temp_addr.s_addr = addr;
	temp_addr2.s_addr = broadcast;
	s_tmp = g_strdup (inet_ntoa (temp_addr));
	s_tmp2 = g_strdup (inet_ntoa (temp_addr2));
	buf = g_strdup_printf ("/sbin/ip addr add %s/%d brd %s dev %s label %s", s_tmp, prefix, s_tmp2, iface, iface);
	g_free (s_tmp);
	g_free (s_tmp2);
	if ((err = nm_spawn_process (buf)))
	{
		syslog (LOG_ERR, "Error: could not set network configuration for device '%s' using command:\n     '%s'", iface, buf);
		goto error;
	}
	g_free (buf);

	/* Alert other computers of our new address */
	temp_addr.s_addr = addr;
	buf = g_strdup_printf ("/sbin/arping -q -A -c 1 -I %s %s", iface, inet_ntoa (temp_addr));
	nm_spawn_process (buf);
	g_free (buf);
	g_usleep (G_USEC_PER_SEC * 2);
	buf = g_strdup_printf ("/sbin/arping -q -U -c 1 -I %s %s", iface, inet_ntoa (temp_addr));
	nm_spawn_process (buf);
	g_free (buf);

	/* Set the default route to be this device's gateway */
	temp_addr.s_addr = nm_device_config_get_ip4_gateway (dev);
	buf = g_strdup_printf ("/sbin/ip route replace default via %s dev %s", inet_ntoa (temp_addr), iface);
	if ((err = nm_spawn_process (buf)))
	{
		syslog (LOG_ERR, "Error: could not set default route using command\n     '%s'", buf);
		goto error;
	}
	g_free (buf);
	return (TRUE);

error:
	g_free (buf);
	nm_system_device_flush_addresses (dev);
	nm_system_device_flush_routes (dev);
	return (FALSE);
}


/*
 * nm_system_enable_loopback
 *
 * Bring up the loopback interface
 *
 */
void nm_system_enable_loopback (void)
{
	nm_spawn_process ("/sbin/ip link set dev lo up");
	nm_spawn_process ("/sbin/ip addr add 127.0.0.1/8 brd 127.255.255.255 dev lo scope host label loopback");
}


/*
 * nm_system_delete_default_route
 *
 * Remove the old default route in preparation for a new one
 *
 */
void nm_system_delete_default_route (void)
{
	nm_spawn_process ("/sbin/ip route del default");
}


/*
 * nm_system_kill_all_dhcp_daemons
 *
 * Kill all DHCP daemons currently running, done at startup.
 *
 */
void nm_system_kill_all_dhcp_daemons (void)
{
	nm_spawn_process ("/usr/bin/killall -q dhclient");
}


/*
 * nm_system_update_dns
 *
 * Invalidate the nscd host cache, if it exists, since
 * we changed resolv.conf.
 *
 */
void nm_system_update_dns (void)
{
	if(nm_spawn_process ("/etc/init.d/nscd status"))
		nm_spawn_process ("nscd -i hosts");
}


/*
 * nm_system_load_device_modules
 *
 * Load any network adapter kernel modules that we need to, since Fedora doesn't
 * autoload them at this time.
 *
 */
void nm_system_load_device_modules (void)
{
	nm_spawn_process ("/usr/bin/NMLoadModules");
}


/*
 * nm_system_restart_mdns_responder
 *
 * Restart the multicast DNS responder so that it knows about new
 * network interfaces and IP addresses.
 *
 */
void nm_system_restart_mdns_responder (void)
{
	FILE 		*fp  = NULL;

	if ((fp = fopen ("/var/run/mDNSResponder.pid", "rt")))
	{
		int pid;
		int res = fscanf (fp, "%d", &pid);
		fclose (fp);
		if (res == 1)
		{
			syslog (LOG_INFO, "Restarting mDNSResponder.\n");
			kill (pid, SIGUSR1);
		}
	}
}


/*
 * nm_system_device_update_config_info
 *
 * Retrieve any relevant configuration info for a particular device
 * from the system network configuration information.  Clear out existing
 * info before setting stuff too.
 *
 */
void nm_system_device_update_config_info (NMDevice *dev)
{
	char		*cfg_file_path = NULL;
	shvarFile *file;
	char		*buf = NULL;
	gboolean	 use_dhcp = TRUE;
	guint32	 ip4_address = 0;
	guint32	 ip4_netmask = 0;
	guint32	 ip4_gateway = 0;
	guint32	 ip4_broadcast = 0;

	g_return_if_fail (dev != NULL);

	/* We use DHCP on an interface unless told not to */
	nm_device_config_set_use_dhcp (dev, TRUE);
	nm_device_config_set_ip4_address (dev, 0);
	nm_device_config_set_ip4_gateway (dev, 0);
	nm_device_config_set_ip4_netmask (dev, 0);
	nm_device_config_set_ip4_broadcast (dev, 0);

	/* Red Hat/Fedora Core systems store this information in
	 * /etc/sysconfig/network-scripts/ifcfg-* where * is the interface
	 * name.
	 */

	cfg_file_path = g_strdup_printf ("/etc/sysconfig/network-scripts/ifcfg-%s", nm_device_get_iface (dev));
	if (!cfg_file_path)
		return;

	if (!(file = svNewFile (cfg_file_path)))
	{
		g_free (cfg_file_path);
		return;
	}
	g_free (cfg_file_path);

	/* Make sure this config file is for this device */
	buf = svGetValue (file, "DEVICE");
	if (!buf || strcmp (buf, nm_device_get_iface (dev)))
	{
		free (buf);
		goto out;
	}

	buf = svGetValue (file, "BOOTPROTO");
	if (buf)
	{
		if (strcmp (buf, "dhcp"))
			use_dhcp = FALSE;
		free (buf);
	}

	buf = svGetValue (file, "IPADDR");
	if (buf)
	{
		ip4_address = inet_addr (buf);
		free (buf);
	}

	buf = svGetValue (file, "GATEWAY");
	if (buf)
	{
		ip4_gateway = inet_addr (buf);
		free (buf);
	}

	buf = svGetValue (file, "NETMASK");
	if (buf)
	{
		ip4_netmask = inet_addr (buf);
		free (buf);
	}
	else
	{
		/* Make a default netmask if we have an IP address */
		if (ip4_address)
		{
			if (((ntohl (ip4_address) & 0xFF000000) >> 24) <= 127)
				ip4_netmask = htonl (0xFF000000);
			else if (((ntohl (ip4_address) & 0xFF000000) >> 24) <= 191)
				ip4_netmask = htonl (0xFFFF0000);
			else
				ip4_netmask = htonl (0xFFFFFF00);
		}
	}

	buf = svGetValue (file, "BROADCAST");
	if (buf)
	{
		ip4_broadcast = inet_addr (buf);
		free (buf);
	}

	if (!use_dhcp && (!ip4_address || !ip4_gateway || !ip4_netmask))
	{
		syslog (LOG_ERR, "Error: network configuration for device '%s' was invalid (non-DCHP configuration,"
						" but no address/gateway specificed).  Will use DHCP instead.\n", nm_device_get_iface (dev));
		use_dhcp = TRUE;
	}

	/* If successful, set values on the device */
	nm_device_config_set_use_dhcp (dev, use_dhcp);
	if (ip4_address)
		nm_device_config_set_ip4_address (dev, ip4_address);
	if (ip4_gateway)
		nm_device_config_set_ip4_gateway (dev, ip4_gateway);
	if (ip4_netmask)
		nm_device_config_set_ip4_netmask (dev, ip4_netmask);
	if (ip4_broadcast)
		nm_device_config_set_ip4_broadcast (dev, ip4_broadcast);

#if 0
	syslog (LOG_DEBUG, "------ Config (%s)", nm_device_get_iface (dev));
	syslog (LOG_DEBUG, "    DHCP=%d\n", use_dhcp);
	syslog (LOG_DEBUG, "    ADDR=%d\n", ip4_address);
	syslog (LOG_DEBUG, "    GW=%d\n", ip4_gateway);
	syslog (LOG_DEBUG, "    NM=%d\n", ip4_netmask);
	syslog (LOG_DEBUG, "---------------------\n");
#endif

out:
	svCloseFile (file);
}
