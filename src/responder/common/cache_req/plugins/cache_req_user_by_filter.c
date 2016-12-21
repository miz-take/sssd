/*
    Authors:
        Pavel Březina <pbrezina@redhat.com>

    Copyright (C) 2016 Red Hat

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <talloc.h>
#include <ldb.h>

#include "db/sysdb.h"
#include "util/util.h"
#include "providers/data_provider.h"
#include "responder/common/cache_req/cache_req_plugin.h"

static errno_t
cache_req_user_by_filter_prepare_domain_data(struct cache_req *cr,
                                             struct cache_req_data *data,
                                             struct sss_domain_info *domain)
{
    TALLOC_CTX *tmp_ctx;
    const char *name;
    errno_t ret;

    if (cr->data->name.name == NULL) {
        DEBUG(SSSDBG_CRIT_FAILURE, "Bug: parsed name is NULL?\n");
        return ERR_INTERNAL;
    }

    tmp_ctx = talloc_new(NULL);
    if (tmp_ctx == NULL) {
        return ENOMEM;
    }

    name = sss_get_cased_name(tmp_ctx, cr->data->name.name,
                              domain->case_sensitive);
    if (name == NULL) {
        ret = ENOMEM;
        goto done;
    }

    name = sss_reverse_replace_space(tmp_ctx, name, cr->rctx->override_space);
    if (name == NULL) {
        ret = ENOMEM;
        goto done;
    }

    talloc_zfree(data->name.lookup);
    data->name.lookup = talloc_steal(data, name);

    ret = EOK;

done:
    talloc_free(tmp_ctx);
    return ret;
}

static const char *
cache_req_user_by_filter_create_debug_name(TALLOC_CTX *mem_ctx,
                                           struct cache_req_data *data,
                                           struct sss_domain_info *domain)
{
    return talloc_strdup(mem_ctx, data->name.lookup);
}

static errno_t
cache_req_user_by_filter_lookup(TALLOC_CTX *mem_ctx,
                                struct cache_req *cr,
                                struct cache_req_data *data,
                                struct sss_domain_info *domain,
                                struct ldb_result **_result)
{
    char *recent_filter;
    errno_t ret;

    recent_filter = talloc_asprintf(mem_ctx, "(%s>=%lu)", SYSDB_LAST_UPDATE,
                                    cr->req_start);
    if (recent_filter == NULL) {
        return ENOMEM;
    }

    ret = sysdb_enumpwent_filter_with_views(mem_ctx, domain, data->name.lookup,
                                            recent_filter, _result);
    talloc_free(recent_filter);

    return ret;
}

static errno_t
cache_req_user_by_filter_dpreq_params(TALLOC_CTX *mem_ctx,
                                      struct cache_req *cr,
                                      struct ldb_result *result,
                                      const char **_string,
                                      uint32_t *_id,
                                      const char **_flag)
{
    *_id = cr->data->id;
    *_string = cr->data->name.lookup;
    *_flag = NULL;

    return EOK;
}

const struct cache_req_plugin cache_req_user_by_filter = {
    .name = "User by filter",
    .dp_type = SSS_DP_WILDCARD_USER,
    .attr_expiration = SYSDB_CACHE_EXPIRE,
    .parse_name = true,
    .bypass_cache = true,
    .only_one_result = false,
    .search_all_domains = false,
    .require_enumeration = false,
    .allow_missing_fqn = false,
    .allow_switch_to_upn = false,
    .upn_equivalent = CACHE_REQ_SENTINEL,
    .get_next_domain_flags = 0,

    .is_well_known_fn = NULL,
    .prepare_domain_data_fn = cache_req_user_by_filter_prepare_domain_data,
    .create_debug_name_fn = cache_req_user_by_filter_create_debug_name,
    .global_ncache_add_fn = NULL,
    .ncache_check_fn = NULL,
    .ncache_add_fn = NULL,
    .lookup_fn = cache_req_user_by_filter_lookup,
    .dpreq_params_fn = cache_req_user_by_filter_dpreq_params
};

struct tevent_req *
cache_req_user_by_filter_send(TALLOC_CTX *mem_ctx,
                              struct tevent_context *ev,
                              struct resp_ctx *rctx,
                              const char *domain,
                              const char *filter)
{
    struct cache_req_data *data;

    data = cache_req_data_name(mem_ctx, CACHE_REQ_USER_BY_FILTER, filter);
    if (data == NULL) {
        return NULL;
    }

    return cache_req_steal_data_and_send(mem_ctx, ev, rctx, NULL,
                                         0, domain, data);
}