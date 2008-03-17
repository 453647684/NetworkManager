/* -*- Mode: C; tab-width: 5; indent-tabs-mode: t; c-basic-offset: 5 -*- */

#include <string.h>
#include <ctype.h>
#include <dbus/dbus-glib.h>
#include "nm-setting-8021x.h"
#include "nm-param-spec-specialized.h"
#include "nm-utils.h"

G_DEFINE_TYPE (NMSetting8021x, nm_setting_802_1x, NM_TYPE_SETTING)

enum {
	PROP_0,
	PROP_EAP,
	PROP_IDENTITY,
	PROP_ANONYMOUS_IDENTITY,
	PROP_CA_CERT,
	PROP_CA_PATH,
	PROP_CLIENT_CERT,
	PROP_PHASE1_PEAPVER,
	PROP_PHASE1_PEAPLABEL,
	PROP_PHASE1_FAST_PROVISIONING,
	PROP_PHASE2_AUTH,
	PROP_PHASE2_AUTHEAP,
	PROP_PHASE2_CA_CERT,
	PROP_PHASE2_CA_PATH,
	PROP_PHASE2_CLIENT_CERT,
	PROP_PASSWORD,
	PROP_PRIVATE_KEY,
	PROP_PHASE2_PRIVATE_KEY,
	PROP_PIN,
	PROP_PSK,

	LAST_PROP
};

NMSetting *
nm_setting_802_1x_new (void)
{
	return (NMSetting *) g_object_new (NM_TYPE_SETTING_802_1X, NULL);
}

static void
need_secrets_password (NMSetting8021x *self,
                       GPtrArray *secrets,
                       gboolean phase2)
{
	if (!self->password || !strlen (self->password))
		g_ptr_array_add (secrets, NM_SETTING_802_1X_PASSWORD);
}

static void
need_secrets_sim (NMSetting8021x *self,
                  GPtrArray *secrets,
                  gboolean phase2)
{
	if (!self->pin || !strlen (self->pin))
		g_ptr_array_add (secrets, NM_SETTING_802_1X_PIN);
}

static void
need_secrets_tls (NMSetting8021x *self,
                  GPtrArray *secrets,
                  gboolean phase2)
{
	if (phase2) {
		if (   self->phase2_client_cert
		    && (!self->phase2_private_key || !self->phase2_private_key->len))
			g_ptr_array_add (secrets, NM_SETTING_802_1X_PHASE2_PRIVATE_KEY);
	} else {
		if (self->client_cert
		    && (!self->private_key || !self->private_key->len))
			g_ptr_array_add (secrets, NM_SETTING_802_1X_PRIVATE_KEY);
	}
}

static gboolean
verify_tls (NMSetting8021x *self, gboolean phase2)
{
	if (phase2) {
		if (!self->phase2_client_cert || !self->phase2_client_cert->len)
			return FALSE;
	} else {
		if (!self->client_cert || !self->client_cert->len)
			return FALSE;
	}

	return TRUE;
}

static gboolean
verify_ttls (NMSetting8021x *self, gboolean phase2)
{
	if (!self->identity && !self->anonymous_identity)
		return FALSE;

	if (!self->phase2_auth && !self->phase2_autheap)
		return FALSE;

	return TRUE;
}

static gboolean
verify_identity (NMSetting8021x *self, gboolean phase2)
{
	return self->identity ? TRUE : FALSE;
}

/* Implemented below... */
static void need_secrets_phase2 (NMSetting8021x *self,
                                 GPtrArray *secrets,
                                 gboolean phase2);


typedef void (*EAPMethodNeedSecretsFunc) (NMSetting8021x *self,
                                          GPtrArray *secrets,
                                          gboolean phase2);

typedef gboolean (*EAPMethodValidateFunc)(NMSetting8021x *self,
                                          gboolean phase2);

typedef struct {
	const char *method;
	EAPMethodNeedSecretsFunc ns_func;
	EAPMethodValidateFunc v_func;
} EAPMethodsTable;

static EAPMethodsTable eap_methods_table[] = {
	{ "leap", need_secrets_password, verify_identity },
	{ "md5", need_secrets_password, verify_identity },
	{ "pap", need_secrets_password, verify_identity },
	{ "chap", need_secrets_password, verify_identity },
	{ "mschap", need_secrets_password, verify_identity },
	{ "mschapv2", need_secrets_password, verify_identity },
	{ "fast", need_secrets_password, verify_identity },
	{ "tls", need_secrets_tls, verify_tls },
	{ "peap", need_secrets_phase2, NULL },
	{ "ttls", need_secrets_phase2, verify_ttls },
	{ "sim", need_secrets_sim, NULL },
	{ "gtc", NULL, NULL },  // FIXME: implement
	{ "otp", NULL, NULL },  // FIXME: implement
	{ NULL, NULL, NULL }
};

static void
need_secrets_phase2 (NMSetting8021x *self,
                     GPtrArray *secrets,
                     gboolean phase2)
{
	char *method = NULL;
	int i;

	g_return_if_fail (phase2 == FALSE);

	/* Check phase2_auth and phase2_autheap */
	method = self->phase2_auth;
	if (!method && self->phase2_autheap)
		method = self->phase2_autheap;

	if (!method) {
		g_warning ("Couldn't find EAP method.");
		g_assert_not_reached();
		return;
	}

	/* Ask the configured phase2 method if it needs secrets */
	for (i = 0; eap_methods_table[i].method; i++) {
		if (eap_methods_table[i].ns_func == NULL)
			continue;
		if (strcmp (eap_methods_table[i].method, method)) {
			(*eap_methods_table[i].ns_func) (self, secrets, TRUE);
			break;
		}
	}
}


static GPtrArray *
need_secrets (NMSetting *setting)
{
	NMSetting8021x *self = NM_SETTING_802_1X (setting);
	GSList *iter;
	GPtrArray *secrets;
	gboolean eap_method_found = FALSE;

	secrets = g_ptr_array_sized_new (4);

	/* Ask each configured EAP method if it needs secrets */
	for (iter = self->eap; iter && !eap_method_found; iter = g_slist_next (iter)) {
		const char *method = (const char *) iter->data;
		int i;

		for (i = 0; eap_methods_table[i].method; i++) {
			if (eap_methods_table[i].ns_func == NULL)
				continue;
			if (!strcmp (eap_methods_table[i].method, method)) {
				(*eap_methods_table[i].ns_func) (self, secrets, FALSE);

				/* Only break out of the outer loop if this EAP method
				 * needed secrets.
				 */
				if (secrets->len > 0)
					eap_method_found = TRUE;
				break;
			}
		}
	}

	if (secrets->len == 0) {
		g_ptr_array_free (secrets, TRUE);
		secrets = NULL;
	}

	return secrets;
}

static gboolean
verify (NMSetting *setting, GSList *all_settings)
{
	NMSetting8021x *self = NM_SETTING_802_1X (setting);
	const char *valid_eap[] = { "leap", "md5", "tls", "peap", "ttls", "sim", "fast", NULL };
	const char *valid_phase1_peapver[] = { "0", "1", NULL };
	const char *valid_phase2_auth[] = { "pap", "chap", "mschap", "mschapv2", "gtc", "otp", "md5", "tls", NULL };
	const char *valid_phase2_autheap[] = { "md5", "mschapv2", "otp", "gtc", "tls", NULL };
	GSList *iter;

	if (!self->eap) {
		g_warning ("Missing eap method");
		return FALSE;
	}

	if (!nm_utils_string_slist_validate (self->eap, valid_eap)) {
		g_warning ("Invalid eap");
		return FALSE;
	}

	/* Ask each configured EAP method if its valid */
	for (iter = self->eap; iter; iter = g_slist_next (iter)) {
		const char *method = (const char *) iter->data;
		int i;

		for (i = 0; eap_methods_table[i].method; i++) {
			if (eap_methods_table[i].v_func == NULL)
				continue;
			if (!strcmp (eap_methods_table[i].method, method)) {
				if (!(*eap_methods_table[i].v_func) (self, FALSE))
					return FALSE;
				break;
			}
		}
	}

	if (self->phase1_peapver && !nm_utils_string_in_list (self->phase1_peapver, valid_phase1_peapver)) {
		g_warning ("Invalid phase1 peapver");
		return FALSE;
	}

	if (self->phase1_peaplabel && strcmp (self->phase1_peaplabel, "1")) {
		g_warning ("Invalid phase1 peaplabel");
		return FALSE;
	}

	if (self->phase1_fast_provisioning && strcmp (self->phase1_fast_provisioning, "1")) {
		g_warning ("Invalid phase1 fast provisioning");
		return FALSE;
	}

	if (self->phase2_auth && !nm_utils_string_in_list (self->phase2_auth, valid_phase2_auth)) {
		g_warning ("Invalid phase2 authentication");
		return FALSE;
	}

	if (self->phase2_autheap && !nm_utils_string_in_list (self->phase2_autheap, valid_phase2_autheap)) {
		g_warning ("Invalid phase2 autheap");
		return FALSE;
	}

	/* FIXME: finish */

	return TRUE;
}

static void
nm_setting_802_1x_init (NMSetting8021x *setting)
{
	((NMSetting *) setting)->name = g_strdup (NM_SETTING_802_1X_SETTING_NAME);
}

static void
finalize (GObject *object)
{
	NMSetting8021x *self = NM_SETTING_802_1X (object);

	/* Strings first. g_free() already checks for NULLs so we don't have to */

	g_free (self->identity);
	g_free (self->anonymous_identity);
	g_free (self->ca_path);
	g_free (self->phase1_peapver);
	g_free (self->phase1_peaplabel);
	g_free (self->phase1_fast_provisioning);
	g_free (self->phase2_auth);
	g_free (self->phase2_autheap);
	g_free (self->phase2_ca_path);
	g_free (self->password);

	nm_utils_slist_free (self->eap, g_free);

	if (self->ca_cert)
		g_byte_array_free (self->ca_cert, TRUE);
	if (self->client_cert)
		g_byte_array_free (self->client_cert, TRUE);
	if (self->private_key)
		g_byte_array_free (self->private_key, TRUE);
	if (self->phase2_ca_cert)
		g_byte_array_free (self->phase2_ca_cert, TRUE);
	if (self->phase2_client_cert)
		g_byte_array_free (self->phase2_client_cert, TRUE);
	if (self->phase2_private_key)
		g_byte_array_free (self->phase2_private_key, TRUE);

	G_OBJECT_CLASS (nm_setting_802_1x_parent_class)->finalize (object);
}

static void
set_property (GObject *object, guint prop_id,
		    const GValue *value, GParamSpec *pspec)
{
	NMSetting8021x *setting = NM_SETTING_802_1X (object);

	switch (prop_id) {
	case PROP_EAP:
		nm_utils_slist_free (setting->eap, g_free);
		setting->eap = g_value_dup_boxed (value);
		break;
	case PROP_IDENTITY:
		g_free (setting->identity);
		setting->identity = g_value_dup_string (value);
		break;
	case PROP_ANONYMOUS_IDENTITY:
		g_free (setting->anonymous_identity);
		setting->anonymous_identity = g_value_dup_string (value);
		break;
	case PROP_CA_CERT:
		if (setting->ca_cert)
			g_byte_array_free (setting->ca_cert, TRUE);
		setting->ca_cert = g_value_dup_boxed (value);
		break;
	case PROP_CA_PATH:
		g_free (setting->ca_path);
		setting->ca_path = g_value_dup_string (value);
		break;
	case PROP_CLIENT_CERT:
		if (setting->client_cert)
			g_byte_array_free (setting->client_cert, TRUE);
		setting->client_cert = g_value_dup_boxed (value);
		break;
	case PROP_PHASE1_PEAPVER:
		g_free (setting->phase1_peapver);
		setting->phase1_peapver = g_value_dup_string (value);
		break;
	case PROP_PHASE1_PEAPLABEL:
		g_free (setting->phase1_peaplabel);
		setting->phase1_peaplabel = g_value_dup_string (value);
		break;
	case PROP_PHASE1_FAST_PROVISIONING:
		g_free (setting->phase1_fast_provisioning);
		setting->phase1_fast_provisioning = g_value_dup_string (value);
		break;
	case PROP_PHASE2_AUTH:
		g_free (setting->phase2_auth);
		setting->phase2_auth = g_value_dup_string (value);
		break;
	case PROP_PHASE2_AUTHEAP:
		g_free (setting->phase2_autheap);
		setting->phase2_autheap = g_value_dup_string (value);
		break;
	case PROP_PHASE2_CA_CERT:
		if (setting->phase2_ca_cert)
			g_byte_array_free (setting->phase2_ca_cert, TRUE);
		setting->phase2_ca_cert = g_value_dup_boxed (value);
		break;
	case PROP_PHASE2_CA_PATH:
		g_free (setting->phase2_ca_path);
		setting->phase2_ca_path = g_value_dup_string (value);
		break;
	case PROP_PHASE2_CLIENT_CERT:
		if (setting->phase2_client_cert)
			g_byte_array_free (setting->phase2_client_cert, TRUE);
		setting->phase2_client_cert = g_value_dup_boxed (value);
		break;
	case PROP_PASSWORD:
		g_free (setting->password);
		setting->password = g_value_dup_string (value);
		break;
	case PROP_PRIVATE_KEY:
		if (setting->private_key)
			g_byte_array_free (setting->private_key, TRUE);
		setting->private_key = g_value_dup_boxed (value);
		break;
	case PROP_PHASE2_PRIVATE_KEY:
		if (setting->phase2_private_key)
			g_byte_array_free (setting->phase2_private_key, TRUE);
		setting->phase2_private_key = g_value_dup_boxed (value);
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
	NMSetting8021x *setting = NM_SETTING_802_1X (object);

	switch (prop_id) {
	case PROP_EAP:
		g_value_set_boxed (value, setting->eap);
		break;
	case PROP_IDENTITY:
		g_value_set_string (value, setting->identity);
		break;
	case PROP_ANONYMOUS_IDENTITY:
		g_value_set_string (value, setting->anonymous_identity);
		break;
	case PROP_CA_CERT:
		g_value_set_boxed (value, setting->ca_cert);
		break;
	case PROP_CA_PATH:
		g_value_set_string (value, setting->ca_path);
		break;
	case PROP_CLIENT_CERT:
		g_value_set_boxed (value, setting->client_cert);
		break;
	case PROP_PHASE1_PEAPVER:
		g_value_set_string (value, setting->phase1_peapver);
		break;
	case PROP_PHASE1_PEAPLABEL:
		g_value_set_string (value, setting->phase1_peaplabel);
		break;
	case PROP_PHASE1_FAST_PROVISIONING:
		g_value_set_string (value, setting->phase1_fast_provisioning);
		break;
	case PROP_PHASE2_AUTH:
		g_value_set_string (value, setting->phase2_auth);
		break;
	case PROP_PHASE2_AUTHEAP:
		g_value_set_string (value, setting->phase2_autheap);
		break;
	case PROP_PHASE2_CA_CERT:
		g_value_set_boxed (value, setting->phase2_ca_cert);
		break;
	case PROP_PHASE2_CA_PATH:
		g_value_set_string (value, setting->phase2_ca_path);
		break;
	case PROP_PHASE2_CLIENT_CERT:
		g_value_set_boxed (value, setting->phase2_client_cert);
		break;
	case PROP_PASSWORD:
		g_value_set_string (value, setting->password);
		break;
	case PROP_PRIVATE_KEY:
		g_value_set_boxed (value, setting->private_key);
		break;
	case PROP_PHASE2_PRIVATE_KEY:
		g_value_set_boxed (value, setting->phase2_private_key);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
nm_setting_802_1x_class_init (NMSetting8021xClass *setting_class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (setting_class);
	NMSettingClass *parent_class = NM_SETTING_CLASS (setting_class);

	/* virtual methods */
	object_class->set_property = set_property;
	object_class->get_property = get_property;
	object_class->finalize     = finalize;

	parent_class->verify         = verify;
	parent_class->need_secrets   = need_secrets;

	/* Properties */
	g_object_class_install_property
		(object_class, PROP_EAP,
		 nm_param_spec_specialized (NM_SETTING_802_1X_EAP,
							   "EAP",
							   "EAP",
							   dbus_g_type_get_collection ("GSList", G_TYPE_STRING),
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_IDENTITY,
		 g_param_spec_string (NM_SETTING_802_1X_IDENTITY,
						  "Identity",
						  "Identity",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_ANONYMOUS_IDENTITY,
		 g_param_spec_string (NM_SETTING_802_1X_ANONYMOUS_IDENTITY,
						  "Anonymous identity",
						  "Anonymous identity",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_CA_CERT,
		 nm_param_spec_specialized (NM_SETTING_802_1X_CA_CERT,
							   "CA certificate",
							   "CA certificate",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_CA_PATH,
		 g_param_spec_string (NM_SETTING_802_1X_CA_PATH,
						  "CA path",
						  "CA path",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_CLIENT_CERT,
		 nm_param_spec_specialized (NM_SETTING_802_1X_CLIENT_CERT,
							   "Client certificate",
							   "Client certificate",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE1_PEAPVER,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE1_PEAPVER,
						  "Phase1 PEAPVER",
						  "Phase1 PEAPVER",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE1_PEAPLABEL,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE1_PEAPLABEL,
						  "Phase1 PEAP label",
						  "Phase1 PEAP label",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE1_FAST_PROVISIONING,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE1_FAST_PROVISIONING,
						  "Phase1 fast provisioning",
						  "Phase1 fast provisioning",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE2_AUTH,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE2_AUTH,
						  "Phase2 auth",
						  "Phase2 auth",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE2_AUTHEAP,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE2_AUTHEAP,
						  "Phase2 autheap",
						  "Phase2 autheap",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE2_CA_CERT,
		 nm_param_spec_specialized (NM_SETTING_802_1X_PHASE2_CA_CERT,
							   "Phase2 CA certificate",
							   "Phase2 CA certificate",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE2_CA_PATH,
		 g_param_spec_string (NM_SETTING_802_1X_PHASE2_CA_PATH,
						  "Phase2 auth CA path",
						  "Phase2 auth CA path",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PHASE2_CLIENT_CERT,
		 nm_param_spec_specialized (NM_SETTING_802_1X_PHASE2_CLIENT_CERT,
							   "Phase2 client certificate",
							   "Phase2 client certificate",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE));

	g_object_class_install_property
		(object_class, PROP_PASSWORD,
		 g_param_spec_string (NM_SETTING_802_1X_PASSWORD,
						  "Password",
						  "Password",
						  NULL,
						  G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_SECRET));

	g_object_class_install_property
		(object_class, PROP_PRIVATE_KEY,
		 nm_param_spec_specialized (NM_SETTING_802_1X_PRIVATE_KEY,
							   "Private key",
							   "Private key",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_SECRET));

	g_object_class_install_property
		(object_class, PROP_PHASE2_PRIVATE_KEY,
		 nm_param_spec_specialized (NM_SETTING_802_1X_PHASE2_PRIVATE_KEY,
							   "Phase2 private key",
							   "Phase2 private key",
							   DBUS_TYPE_G_UCHAR_ARRAY,
							   G_PARAM_READWRITE | NM_SETTING_PARAM_SERIALIZE | NM_SETTING_PARAM_SECRET));
}
