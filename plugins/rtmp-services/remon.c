#include <obs-module.h>

struct remon_service {
//	bool use_auth;
	char *username, *password;
};

static const char *remon_service_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("Remon StreamingServer");
}

static void remon_service_update(void *data, obs_data_t *settings)
{
	struct remon_service *service = data;

//	service->use_auth = obs_data_get_bool(settings, "use_auth");
	service->username = bstrdup(obs_data_get_string(settings, "username"));
	service->password = bstrdup(obs_data_get_string(settings, "password"));
}

static void remon_service_destroy(void *data)
{
	struct remon_service *service = data;

	bfree(service->username);
	bfree(service->password);
	bfree(service);
}

static void *remon_service_create(obs_data_t *settings, obs_service_t *service)
{
	struct remon_service *data = bzalloc(sizeof(struct remon_service));
	remon_service_update(data, settings);

	UNUSED_PARAMETER(service);
	return data;
}

static bool use_auth_modified(obs_properties_t *ppts, obs_property_t *p,
	obs_data_t *settings)
{
	bool use_auth = obs_data_get_bool(settings, "use_auth");
	p = obs_properties_get(ppts, "username");
	obs_property_set_visible(p, use_auth);
	p = obs_properties_get(ppts, "password");
	obs_property_set_visible(p, use_auth);
	return true;
}

static obs_properties_t *remon_service_properties(void *unused)
{
	UNUSED_PARAMETER(unused);

	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p;

	p = obs_properties_add_bool(ppts, "use_auth", obs_module_text("UseAuth"));
	obs_properties_add_text(ppts, "username", obs_module_text("Username"),
			OBS_TEXT_DEFAULT);
	obs_properties_add_text(ppts, "password", obs_module_text("Password"),
			OBS_TEXT_PASSWORD);
	obs_property_set_modified_callback(p, use_auth_modified);
	return ppts;
}

static const char *remon_service_username(void *data)
{
	struct remon_service *service = data;
	return service->username;
}

static const char *remon_service_password(void *data)
{
	struct remon_service *service = data;
	return service->password;
}

static const char *remon_service_output_type(void *data)
{
	UNUSED_PARAMETER(data);
	return "rm_output";
}

struct obs_service_info remon_service = {
	.id             = "remon",
	.get_name       = remon_service_name,
	.create         = remon_service_create,
	.destroy        = remon_service_destroy,
	.update         = remon_service_update,
	.get_properties = remon_service_properties,
	.get_username   = remon_service_username,
	.get_password   = remon_service_password,
	.get_output_type = remon_service_output_type
};
