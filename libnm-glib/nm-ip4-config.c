#include <string.h>

#include "nm-ip4-config.h"
#include "NetworkManager.h"
#include "nm-types-private.h"
#include "nm-object-private.h"

G_DEFINE_TYPE (NMIP4Config, nm_ip4_config, NM_TYPE_OBJECT)

#define NM_IP4_CONFIG_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), NM_TYPE_IP4_CONFIG, NMIP4ConfigPrivate))

typedef struct {
	DBusGProxy *proxy;

	guint32 address;
	guint32 gateway;
	guint32 netmask;
	guint32 broadcast;
	char *hostname;
	GArray *nameservers;
	GPtrArray *domains;
	char *nis_domain;
	GArray *nis_servers;
} NMIP4ConfigPrivate;

enum {
	PROP_0,
	PROP_ADDRESS,
	PROP_GATEWAY,
	PROP_NETMASK,
	PROP_BROADCAST,
	PROP_HOSTNAME,
	PROP_NAMESERVERS,
	PROP_DOMAINS,
	PROP_NIS_DOMAIN,
	PROP_NIS_SERVERS,

	LAST_PROP
};

static void
nm_ip4_config_init (NMIP4Config *config)
{
}

static gboolean
demarshal_ip4_array (NMObject *object, GParamSpec *pspec, GValue *value, gpointer field)
{
	if (!nm_uint_array_demarshal (value, (GArray **) field))
		return FALSE;

	if (!strcmp (pspec->name, NM_IP4_CONFIG_NAMESERVERS))
		g_object_notify (G_OBJECT (object), NM_IP4_CONFIG_NAMESERVERS);
	else if (!strcmp (pspec->name, NM_IP4_CONFIG_NIS_SERVERS))
		g_object_notify (G_OBJECT (object), NM_IP4_CONFIG_NAMESERVERS);
	return TRUE;
}

static gboolean
demarshal_domains (NMObject *object, GParamSpec *pspec, GValue *value, gpointer field)
{
	if (!nm_string_array_demarshal (value, (GPtrArray **) field))
		return FALSE;

	g_object_notify (G_OBJECT (object), NM_IP4_CONFIG_DOMAINS);
	return TRUE;
}

static void
register_for_property_changed (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	const NMPropertiesChangedInfo property_changed_info[] = {
		{ NM_IP4_CONFIG_ADDRESS,     nm_object_demarshal_generic,  &priv->address },
		{ NM_IP4_CONFIG_GATEWAY,     nm_object_demarshal_generic,  &priv->gateway },
		{ NM_IP4_CONFIG_NETMASK,     nm_object_demarshal_generic,  &priv->netmask },
		{ NM_IP4_CONFIG_BROADCAST,   nm_object_demarshal_generic,  &priv->broadcast },
		{ NM_IP4_CONFIG_HOSTNAME,    nm_object_demarshal_generic,  &priv->hostname },
		{ NM_IP4_CONFIG_NAMESERVERS, demarshal_ip4_array,          &priv->nameservers },
		{ NM_IP4_CONFIG_DOMAINS,     demarshal_domains,            &priv->domains },
		{ NM_IP4_CONFIG_NIS_DOMAIN,  nm_object_demarshal_generic,  &priv->nis_domain },
		{ NM_IP4_CONFIG_NIS_SERVERS, demarshal_ip4_array,          &priv->nis_servers },
		{ NULL },
	};

	nm_object_handle_properties_changed (NM_OBJECT (config),
	                                     priv->proxy,
	                                     property_changed_info);
}

static GObject*
constructor (GType type,
		   guint n_construct_params,
		   GObjectConstructParam *construct_params)
{
	NMObject *object;
	DBusGConnection *connection;
	NMIP4ConfigPrivate *priv;

	object = (NMObject *) G_OBJECT_CLASS (nm_ip4_config_parent_class)->constructor (type,
																 n_construct_params,
																 construct_params);
	if (!object)
		return NULL;

	priv = NM_IP4_CONFIG_GET_PRIVATE (object);
	connection = nm_object_get_connection (object);

	priv->proxy = dbus_g_proxy_new_for_name (connection,
										   NM_DBUS_SERVICE,
										   nm_object_get_path (object),
										   NM_DBUS_INTERFACE_IP4_CONFIG);

	register_for_property_changed (NM_IP4_CONFIG (object));

	return G_OBJECT (object);
}

static void
finalize (GObject *object)
{
	NMIP4ConfigPrivate *priv = NM_IP4_CONFIG_GET_PRIVATE (object);
	int i;

	g_free (priv->hostname);
	g_free (priv->nis_domain);
	if (priv->nameservers)
		g_array_free (priv->nameservers, TRUE);
	if (priv->nis_servers)
		g_array_free (priv->nis_servers, TRUE);

	if (priv->domains) {
		for (i = 0; i < priv->domains->len; i++)
			g_free (g_ptr_array_index (priv->domains, i));
		g_ptr_array_free (priv->domains, TRUE);
	}

	G_OBJECT_CLASS (nm_ip4_config_parent_class)->finalize (object);
}

static void
get_property (GObject *object,
              guint prop_id,
              GValue *value,
              GParamSpec *pspec)
{
	NMIP4Config *self = NM_IP4_CONFIG (object);

	switch (prop_id) {
	case PROP_ADDRESS:
		g_value_set_uint (value, nm_ip4_config_get_address (self));
		break;
	case PROP_GATEWAY:
		g_value_set_uint (value, nm_ip4_config_get_gateway (self));
		break;
	case PROP_NETMASK:
		g_value_set_uint (value, nm_ip4_config_get_netmask (self));
		break;
	case PROP_BROADCAST:
		g_value_set_uint (value, nm_ip4_config_get_broadcast (self));
		break;
	case PROP_HOSTNAME:
		g_value_set_string (value, nm_ip4_config_get_hostname (self));
		break;
	case PROP_NAMESERVERS:
		g_value_set_boxed (value, nm_ip4_config_get_nameservers (self));
		break;
	case PROP_DOMAINS:
		g_value_set_boxed (value, nm_ip4_config_get_domains (self));
		break;
	case PROP_NIS_DOMAIN:
		g_value_set_string (value, nm_ip4_config_get_nis_domain (self));
		break;
	case PROP_NIS_SERVERS:
		g_value_set_boxed (value, nm_ip4_config_get_nis_servers (self));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_ip4_config_class_init (NMIP4ConfigClass *config_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (config_class);

	g_type_class_add_private (config_class, sizeof (NMIP4ConfigPrivate));

	/* virtual methods */
	object_class->constructor = constructor;
	object_class->get_property = get_property;
	object_class->finalize = finalize;

	/* properties */
	g_object_class_install_property
		(object_class, PROP_ADDRESS,
		 g_param_spec_uint (NM_IP4_CONFIG_ADDRESS,
						    "Address",
						    "Address",
						    0, G_MAXUINT32, 0,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_GATEWAY,
		 g_param_spec_uint (NM_IP4_CONFIG_GATEWAY,
						    "Gateway",
						    "Gateway",
						    0, G_MAXUINT32, 0,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NETMASK,
		 g_param_spec_uint (NM_IP4_CONFIG_NETMASK,
						    "Netmask",
						    "Netmask",
						    0, G_MAXUINT32, 0,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_BROADCAST,
		 g_param_spec_uint (NM_IP4_CONFIG_BROADCAST,
						    "Broadcast",
						    "Broadcast",
						    0, G_MAXUINT32, 0,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_HOSTNAME,
		 g_param_spec_string (NM_IP4_CONFIG_HOSTNAME,
						    "Hostname",
						    "Hostname",
						    NULL,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NAMESERVERS,
		 g_param_spec_boxed (NM_IP4_CONFIG_NAMESERVERS,
						    "Nameservers",
						    "Nameservers",
						    NM_TYPE_UINT_ARRAY,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_DOMAINS,
		 g_param_spec_boxed (NM_IP4_CONFIG_DOMAINS,
						    "Domains",
						    "Domains",
						    NM_TYPE_STRING_ARRAY,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NIS_DOMAIN,
		 g_param_spec_string (NM_IP4_CONFIG_NIS_DOMAIN,
						    "NIS domain",
						    "NIS domain",
						    NULL,
						    G_PARAM_READABLE));

	g_object_class_install_property
		(object_class, PROP_NIS_SERVERS,
		 g_param_spec_boxed (NM_IP4_CONFIG_NIS_SERVERS,
						    "NIS servers",
						    "NIS servers",
						    NM_TYPE_UINT_ARRAY,
						    G_PARAM_READABLE));
}

GObject *
nm_ip4_config_new (DBusGConnection *connection, const char *object_path)
{
	return (GObject *) g_object_new (NM_TYPE_IP4_CONFIG,
									 NM_OBJECT_DBUS_CONNECTION, connection,
									 NM_OBJECT_DBUS_PATH, object_path,
									 NULL);
}

guint32
nm_ip4_config_get_address (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->address) {
		priv->address = nm_object_get_uint_property (NM_OBJECT (config),
		                                             NM_DBUS_INTERFACE_IP4_CONFIG,
		                                             "Address");
	}

	return priv->address;
}

guint32
nm_ip4_config_get_gateway (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->gateway) {
		priv->gateway = nm_object_get_uint_property (NM_OBJECT (config),
		                                             NM_DBUS_INTERFACE_IP4_CONFIG,
		                                             "Gateway");
	}

	return priv->gateway;
}

guint32
nm_ip4_config_get_netmask (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->netmask) {
		priv->netmask = nm_object_get_uint_property (NM_OBJECT (config),
		                                             NM_DBUS_INTERFACE_IP4_CONFIG,
		                                             "Netmask");
	}

	return priv->netmask;
}

guint32
nm_ip4_config_get_broadcast (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), 0);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->broadcast) {
		priv->broadcast = nm_object_get_uint_property (NM_OBJECT (config),
		                                               NM_DBUS_INTERFACE_IP4_CONFIG,
		                                               "Broadcast");
	}

	return priv->broadcast;
}

const char *
nm_ip4_config_get_hostname (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->hostname) {
		priv->hostname = nm_object_get_string_property (NM_OBJECT (config),
		                                                NM_DBUS_INTERFACE_IP4_CONFIG,
		                                                "Hostname");
	}

	return priv->hostname;
}

const GArray *
nm_ip4_config_get_nameservers (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	GArray *array = NULL;
	GValue value = {0,};

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->nameservers) {
		if (nm_object_get_property (NM_OBJECT (config),
		                            NM_DBUS_INTERFACE_IP4_CONFIG,
		                            "Nameservers",
		                            &value)) {
			array = (GArray *) g_value_get_boxed (&value);
			if (array && array->len) {
				priv->nameservers = g_array_sized_new (FALSE, TRUE, sizeof (guint32), array->len);
				g_array_append_vals (priv->nameservers, array->data, array->len);
			}
			g_value_unset (&value);
		}
	}

	return priv->nameservers;
}

const GPtrArray *
nm_ip4_config_get_domains (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	GValue value = {0,};

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->domains) {
		if (nm_object_get_property (NM_OBJECT (config),
									NM_DBUS_INTERFACE_IP4_CONFIG,
									"Domains",
									&value)) {
			char **array = NULL, **p;

			array = (char **) g_value_get_boxed (&value);
			if (array && g_strv_length (array)) {
				priv->domains = g_ptr_array_sized_new (g_strv_length (array));
				for (p = array; *p; p++)
					g_ptr_array_add (priv->domains, g_strdup (*p));
			}
			g_value_unset (&value);
		}
	}

	return priv->domains;
}

const char *
nm_ip4_config_get_nis_domain (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->nis_domain) {
		priv->nis_domain = nm_object_get_string_property (NM_OBJECT (config),
		                                                  NM_DBUS_INTERFACE_IP4_CONFIG,
		                                                  "NisDomain");
	}

	return priv->nis_domain;
}

GArray *
nm_ip4_config_get_nis_servers (NMIP4Config *config)
{
	NMIP4ConfigPrivate *priv;
	GArray *array = NULL;
	GValue value = {0,};

	g_return_val_if_fail (NM_IS_IP4_CONFIG (config), NULL);

	priv = NM_IP4_CONFIG_GET_PRIVATE (config);
	if (!priv->nis_servers) {
		if (nm_object_get_property (NM_OBJECT (config),
									NM_DBUS_INTERFACE_IP4_CONFIG,
									"NisServers",
									&value)) {
			array = (GArray *) g_value_get_boxed (&value);
			if (array && array->len) {
				priv->nis_servers = g_array_sized_new (FALSE, TRUE, sizeof (guint32), array->len);
				g_array_append_vals (priv->nis_servers, array->data, array->len);
			}
			g_value_unset (&value);
		}
	}

	return priv->nis_servers;
}
