/*
 * Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at:
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <config.h>
#include <errno.h>
#include <linux/psample.h>
#include <sys/poll.h>

#include "dpif-offload-provider.h"
#include "netdev-offload.h"
#include "netlink-protocol.h"
#include "netlink-socket.h"
#include "openvswitch/vlog.h"

VLOG_DEFINE_THIS_MODULE(dpif_offload_netlink);

static struct nl_sock *psample_sock;
static int psample_family;

/* Receive psample netlink message and save the attributes. */
struct offload_psample {
    struct nlattr *packet;      /* packet data */
    int dp_group_id;            /* mapping id for sFlow offload */
    int iifindex;               /* input ifindex */
    int group_seq;              /* group sequence */
};

static void
dpif_offload_netlink_init(void)
{
    unsigned int psample_mcgroup;
    int err;

    if (!netdev_is_flow_api_enabled()) {
        return;
    }

    if (psample_sock) {
        return;
    }

    err = nl_lookup_genl_family(PSAMPLE_GENL_NAME,
                                &psample_family);
    if (err) {
        VLOG_INFO("%s: Generic Netlink family '%s' does not exist. "
                  "Please make sure the kernel module psample is loaded",
                  __func__, PSAMPLE_GENL_NAME);
        return;
    }

    err = nl_lookup_genl_mcgroup(PSAMPLE_GENL_NAME,
                                 PSAMPLE_NL_MCGRP_SAMPLE_NAME,
                                 &psample_mcgroup);
    if (err) {
        VLOG_INFO("%s: Failed to join multicast group '%s' for Generic "
                  "Netlink family '%s'", __func__,
                  PSAMPLE_NL_MCGRP_SAMPLE_NAME,
                  PSAMPLE_GENL_NAME);
        return;
    }

    err = nl_sock_create(NETLINK_GENERIC, &psample_sock);
    if (err) {
        VLOG_INFO("%s: Failed to create psample socket", __func__);
        return;
    }

    err = nl_sock_join_mcgroup(psample_sock, psample_mcgroup);
    if (err) {
        VLOG_INFO("%s: Failed to join psample mcgroup", __func__);
        nl_sock_destroy(psample_sock);
        return;
    }
}

static void
dpif_offload_netlink_uninit(void)
{
    if (!netdev_is_flow_api_enabled()) {
        return;
    }

    if (!psample_sock) {
        return;
    }

    nl_sock_destroy(psample_sock);
    psample_sock = NULL;
}

static void
dpif_offload_netlink_sflow_recv_wait(void)
{
    if (psample_sock) {
        nl_sock_wait(psample_sock, POLLIN);
    }
}

static int
psample_from_ofpbuf(struct offload_psample *psample,
                    const struct ofpbuf *buf)
{
    static const struct nl_policy ovs_psample_policy[] = {
        [PSAMPLE_ATTR_IIFINDEX] = { .type = NL_A_U16 },
        [PSAMPLE_ATTR_SAMPLE_GROUP] = { .type = NL_A_U32 },
        [PSAMPLE_ATTR_GROUP_SEQ] = { .type = NL_A_U32 },
        [PSAMPLE_ATTR_DATA] = { .type = NL_A_UNSPEC },
    };
    struct nlattr *a[ARRAY_SIZE(ovs_psample_policy)];
    struct genlmsghdr *genl;
    struct nlmsghdr *nlmsg;
    struct ofpbuf b;

    b = ofpbuf_const_initializer(buf->data, buf->size);
    nlmsg = ofpbuf_try_pull(&b, sizeof *nlmsg);
    genl = ofpbuf_try_pull(&b, sizeof *genl);
    if (!nlmsg || !genl || nlmsg->nlmsg_type != psample_family
        || !nl_policy_parse(&b, 0, ovs_psample_policy, a,
                            ARRAY_SIZE(ovs_psample_policy))) {
        return EINVAL;
    }

    psample->iifindex = nl_attr_get_u16(a[PSAMPLE_ATTR_IIFINDEX]);
    psample->dp_group_id = nl_attr_get_u32(a[PSAMPLE_ATTR_SAMPLE_GROUP]);
    psample->group_seq = nl_attr_get_u16(a[PSAMPLE_ATTR_GROUP_SEQ]);
    psample->packet = a[PSAMPLE_ATTR_DATA];

    return 0;
}

static int
psample_parse_packet(struct offload_psample *psample,
                     struct dpif_offload_sflow *sflow)
{
    dp_packet_use_stub(&sflow->packet,
                       CONST_CAST(struct nlattr *,
                                  nl_attr_get(psample->packet)) - 1,
                       nl_attr_get_size(psample->packet) +
                       sizeof(struct nlattr));
    dp_packet_set_data(&sflow->packet,
                       (char *) dp_packet_data(&sflow->packet) +
                       sizeof(struct nlattr));
    dp_packet_set_size(&sflow->packet, nl_attr_get_size(psample->packet));

    sflow->attr = dpif_offload_sflow_attr_find(psample->dp_group_id);
    if (!sflow->attr) {
        return ENOENT;
    }
    sflow->iifindex = psample->iifindex;

    return 0;
}

static int
dpif_offload_netlink_sflow_recv(struct dpif_offload_sflow *sflow)
{
    if (!psample_sock) {
        return ENOENT;
    }

    for (;;) {
        static struct vlog_rate_limit rl = VLOG_RATE_LIMIT_INIT(1, 5);
        struct offload_psample psample;
        uint64_t buf_stub[4096 / 8];
        struct ofpbuf buf;
        int error;

        ofpbuf_use_stub(&buf, buf_stub, sizeof buf_stub);
        error = nl_sock_recv(psample_sock, &buf, NULL, false);

        if (!error) {
            error = psample_from_ofpbuf(&psample, &buf);
            if (!error) {
                    ofpbuf_uninit(&buf);
                    error = psample_parse_packet(&psample, sflow);
                    return error;
            }
        } else if (error != EAGAIN) {
            VLOG_WARN_RL(&rl, "%s: error reading or parsing netlink (%s)",
                         __func__, ovs_strerror(error));
            nl_sock_drain(psample_sock);
            error = ENOBUFS;
        }

        ofpbuf_uninit(&buf);
        if (error) {
            return error;
        }
    }
}

const struct dpif_offload_api dpif_offload_netlink = {
    .init = dpif_offload_netlink_init,
    .uninit = dpif_offload_netlink_uninit,
    .sflow_recv_wait = dpif_offload_netlink_sflow_recv_wait,
    .sflow_recv = dpif_offload_netlink_sflow_recv,
};
