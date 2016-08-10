#include "ops-xp-ofproto-provider.h"
#include "ops-xp-ofproto-datapath.h"
#include "openvswitch/vlog.h"
#include "ofpbuf.h"

VLOG_DEFINE_THIS_MODULE(ops_xp_ofproto_datapath);

static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);

struct rule *
ofproto_xpliant_rule_alloc(void)
{
    struct rule_xpliant *rule = xmalloc(sizeof *rule);

    return &rule->up;
}

void
ofproto_xpliant_rule_dealloc(struct rule *rule_)
{
    struct rule_xpliant *rule = rule_xpliant_cast(rule_);

    free(rule);
}

enum ofperr
ofproto_xpliant_rule_construct(struct rule *rule_ OVS_UNUSED)
    OVS_NO_THREAD_SAFETY_ANALYSIS
{
    return 0;
}

void
ofproto_xpliant_rule_insert(struct rule *rule, struct rule *old_rule,
                            bool forward_stats)
OVS_REQUIRES(ofproto_mutex)
{
    return;
}

void
ofproto_xpliant_rule_delete(struct rule *rule_ OVS_UNUSED)
OVS_REQUIRES(ofproto_mutex)
{
    return;
}

void
ofproto_xpliant_rule_destruct(struct rule *rule_ OVS_UNUSED)
{
    return;
}

void
ofproto_xpliant_rule_get_stats(struct rule *rule_ OVS_UNUSED,
                               uint64_t * packets OVS_UNUSED,
                               uint64_t * bytes OVS_UNUSED,
                               long long int *used OVS_UNUSED)
{
    return;
}

enum ofperr
ofproto_xpliant_rule_execute(struct rule *rule OVS_UNUSED,
                             const struct flow *flow OVS_UNUSED,
                             struct dp_packet *packet OVS_UNUSED)
{
    return 0;
}

bool
ofproto_xpliant_set_frag_handling(struct ofproto *ofproto_ OVS_UNUSED,
                                  enum ofp_config_flags frag_handling OVS_UNUSED)
{
    return false;
}

enum ofperr
ofproto_xpliant_packet_out(struct ofproto *ofproto_ OVS_UNUSED,
                           struct dp_packet *packet OVS_UNUSED,
                           const struct flow *flow OVS_UNUSED,
                           const struct ofpact *ofpacts OVS_UNUSED,
                           size_t ofpacts_len OVS_UNUSED)
{
    return 0;
}

struct ofgroup *
ofproto_xpliant_group_alloc(void)
{
    struct group_xpliant *group = xzalloc(sizeof *group);

    return &group->up;
}

void
ofproto_xpliant_group_dealloc(struct ofgroup *group_)
{
    struct group_xpliant *group = group_xpliant_cast(group_);

    free(group);
}

enum ofperr
ofproto_xpliant_group_construct(struct ofgroup *group_ OVS_UNUSED)
{
    return 0;
}

void
ofproto_xpliant_group_destruct(struct ofgroup *group_ OVS_UNUSED)
{
    return;
}

enum ofperr
ofproto_xpliant_group_modify(struct ofgroup *group_ OVS_UNUSED)
{
    return 0;
}

enum ofperr
ofproto_xpliant_group_get_stats(const struct ofgroup *group_ OVS_UNUSED,
                                struct ofputil_group_stats *ogs OVS_UNUSED)
{
    return 0;
}

const char *
ofproto_xpliant_get_datapath_version(const struct ofproto *ofproto_ OVS_UNUSED)
{
    return "0.0.1";
}
