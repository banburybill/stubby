#include "config.h"

#include <errno.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

#include <getdns/getdns.h>
#include <getdns/getdns_extra.h>

#include "configfile.h"
#include "log.h"
#include "util.h"

#ifdef HAVE_GETDNS_YAML2DICT
getdns_return_t getdns_yaml2dict(const char *str, getdns_dict **dict);
#else
# include "yaml/convert_yaml_to_json.h"
# define getdns_yaml2dict stubby_yaml2dict
getdns_return_t
getdns_yaml2dict(const char *str, getdns_dict **dict)
{
        char *jsonstr;

        if (!str || !dict)
                return GETDNS_RETURN_INVALID_PARAMETER;

        jsonstr = yaml_string_to_json_string(str);
        if (jsonstr) {
                getdns_return_t res = getdns_str2dict(jsonstr, dict);
                free(jsonstr);
                return res;
        } else {
                return GETDNS_RETURN_GENERIC_ERROR;
        }
}
#endif

static char *make_config_file_path(const char *dir, const char *fname)
{
        int reslen = strlen(dir) + strlen(fname) + 1;
        char *res = malloc(reslen);

        if (res == NULL)
                return NULL;

        snprintf(res, reslen, "%s%s", dir, fname);
        return res;
}

#if defined(STUBBY_ON_WINDOWS)

static char *folder_config_file(int csidl)
{
        TCHAR szPath[MAX_PATH];

        if (!SUCCEEDED(SHGetFolderPath(NULL,
            csidl | CSIDL_FLAG_CREATE, NULL, 0, szPath)))
                return NULL;

        return make_config_file_path(szPath, "\\Stubby\\stubby.yml");
}

// %APPDATA%/Stubby/stubby.yml.
char *home_config_file(void)
{
        return folder_config_file(CSIDL_APPDATA);
}

char *system_config_file(void)
{
        return folder_config_file(CSIDL_PROGRAM_FILES);
}

#else

char *home_config_file(void)
{
        struct passwd *p = getpwuid(getuid());
        char *home = p ? p->pw_dir : getenv("HOME");
        if (!home)
                return NULL;
        return make_config_file_path(home, "/.stubby.yml");
}

char *system_config_file(void)
{
        return make_config_file_path(STUBBYCONFDIR, "/stubby.yml");
}

#endif

static const char *default_config =
"{ resolution_type: GETDNS_RESOLUTION_STUB"
", dns_transport_list: [ GETDNS_TRANSPORT_TLS"
"                      , GETDNS_TRANSPORT_UDP"
"                      , GETDNS_TRANSPORT_TCP ]"
", idle_timeout: 10000"
", listen_addresses: [ 127.0.0.1@53, 0::1@53 ]"
", tls_query_padding_blocksize: 256"
", edns_client_subnet_private : 1"
", round_robin_upstreams: 1"
"}";

static getdns_dict *listen_dict = NULL;
static getdns_list *listen_list = NULL;

static getdns_return_t parse_config(getdns_context *context, const char *config_str, int yaml_config)
{
        getdns_dict *config_dict;
        getdns_list *list;
        getdns_return_t r;

        if (yaml_config) {
                r = getdns_yaml2dict(config_str, &config_dict);
                if (r == GETDNS_RETURN_NOT_IMPLEMENTED) {
                        /* If this fails then YAML is really not supported. Check this at
                           runtime because it could change under us..... */
                        r = getdns_yaml2dict(config_str, NULL);
                        if (r == GETDNS_RETURN_NOT_IMPLEMENTED) {
                                stubby_error("Support for YAML configuration files not available because\n"
                                             "the version of getdns used was not compiled with YAML support.");
                                return GETDNS_RETURN_NOT_IMPLEMENTED;
                        }
                }
        } else {
                r = getdns_str2dict(config_str, &config_dict);
        }
        if (r) {
                stubby_error("Could not parse config file %s, \"%s\"",
                    config_str, stubby_getdns_strerror(r));
                return r;
        }
        if (!(r = getdns_dict_get_list(
            config_dict, "listen_addresses", &list))) {
                if (listen_list && !listen_dict) {
                        getdns_list_destroy(listen_list);
                        listen_list = NULL;
                }
                /* Strange construction to copy the list.
                 * Needs to be done, because config dict
                 * will get destroyed.
                 */
                if (!listen_dict &&
                    !(listen_dict = getdns_dict_create())) {
                        stubby_error("Could not create listen_dict");
                        r = GETDNS_RETURN_MEMORY_ERROR;

                } else if ((r = getdns_dict_set_list(
                    listen_dict, "listen_list", list)))
                        stubby_error("Could not set listen_list");

                else if ((r = getdns_dict_get_list(
                    listen_dict, "listen_list", &listen_list)))
                        stubby_error("Could not get listen_list");

                (void) getdns_dict_remove_name(
                    config_dict, "listen_addresses");
        }
        if (!r && (r = getdns_context_config(context, config_dict))) {
                stubby_error("Could not configure context with "
                    "config dict: %s", stubby_getdns_strerror(r));
        }
        getdns_dict_destroy(config_dict);
        return r;
}

static getdns_return_t parse_config_file(getdns_context *context, const char *fn)
{
        FILE *fh;
        char *config_file = NULL;
        long config_file_sz;
        size_t read_sz;
        getdns_return_t r;

        if (!(fh = fopen(fn, "r")))
                return GETDNS_RETURN_IO_ERROR;

        if (fseek(fh, 0,SEEK_END) == -1) {
                perror("fseek");
                fclose(fh);
                return GETDNS_RETURN_IO_ERROR;
        }
        config_file_sz = ftell(fh);
        if (config_file_sz <= 0) {
                /* Empty config is no config */
                fclose(fh);
                return GETDNS_RETURN_IO_ERROR;
        }
        if (!(config_file = malloc(config_file_sz + 1))){
                fclose(fh);
                stubby_error("Could not allocate memory for \"%s\"", fn);
                return GETDNS_RETURN_MEMORY_ERROR;
        }
        rewind(fh);
        read_sz = fread(config_file, 1, config_file_sz + 1, fh);
        if (read_sz > (size_t)config_file_sz || ferror(fh) || !feof(fh)) {
                stubby_error("An error occurred while reading \"%s\": %s",
                             fn, strerror(errno));
                fclose(fh);
                free(config_file);
                return GETDNS_RETURN_IO_ERROR;
        }
        config_file[read_sz] = 0;
        fclose(fh);
        r = parse_config(context, config_file,
                         strstr(fn, ".yml") != NULL ||
                         strstr(fn, ".yaml") != NULL);
        free(config_file);
        if (r == GETDNS_RETURN_GOOD)
                stubby_log(NULL,GETDNS_LOG_UPSTREAM_STATS, GETDNS_LOG_DEBUG,
                           "Read config from file %s", fn);
        return r;
}

void init_config(getdns_context *context)
{
        (void) parse_config(context, default_config, 0);
}

void delete_config(void)
{
        /* Listen list is a member of the dict, so will be destroyed */
        /* with the dict. */
        getdns_dict_destroy(listen_dict);
        listen_dict = NULL;
        listen_list = NULL;
}

int read_config(getdns_context *context, const char *custom_config_fn)
{
        char *conf_fn;
        int found_conf = 0;
        getdns_return_t r;

        (void) parse_config(context, default_config, 0);
        if (custom_config_fn) {
                if ((r = parse_config_file(context, custom_config_fn))) {
                        stubby_error("Could not parse config file "
                                     "\"%s\": %s", custom_config_fn,
                                     stubby_getdns_strerror(r));
                        return 0;
                }
        } else {
                conf_fn = home_config_file();
                if (!conf_fn) {
                        stubby_error("Error getting user config file");
                        return 0;
                }
                r = parse_config_file(context, conf_fn);
                if (r == GETDNS_RETURN_GOOD)
                        found_conf = 1;
                else if (r != GETDNS_RETURN_IO_ERROR)
                        stubby_error("Error parsing config file "
                                     "\"%s\": %s", conf_fn
                                     , stubby_getdns_strerror(r));
                free(conf_fn);
                if (!found_conf) {
                        conf_fn = system_config_file();
                        if (!conf_fn) {
                                stubby_error("Error getting system config file");
                                return 0;
                        }
                        r = parse_config_file(context, conf_fn);
                        if (r == GETDNS_RETURN_GOOD)
                                found_conf = 1;
                        else if (r != GETDNS_RETURN_IO_ERROR)
                                stubby_error("Error parsing config file "
                                             "\"%s\": %s", conf_fn
                                             , stubby_getdns_strerror(r));
                        free(conf_fn);
                }
                if (!found_conf)
                        stubby_warning("WARNING: No Stubby config file found... using minimal default config (Opportunistic Usage)");
        }

        return 1;
}

char *get_api_info(getdns_context *context)
{
        getdns_dict *api_information = getdns_context_get_api_information(context);

        if (listen_dict && !getdns_dict_get_list(
                    listen_dict, "listen_list", &listen_list)) {

                (void) getdns_dict_set_list(api_information,
                                            "listen_addresses", listen_list);
        } else if (listen_list) {
                (void) getdns_dict_set_list(api_information,
                                            "listen_addresses", listen_list);

        } else if ((listen_list = getdns_list_create())) {
                (void) getdns_dict_set_list(api_information,
                                            "listen_addresses", listen_list);
                getdns_list_destroy(listen_list);
                listen_list = NULL;
        }

        return getdns_pretty_print_dict(api_information);
}

const getdns_list *get_config_listen_list(void)
{
        return listen_list;
}
