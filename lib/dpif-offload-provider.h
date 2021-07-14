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

#ifndef DPIF_OFFLOAD_PROVIDER_H
#define DPIF_OFFLOAD_PROVIDER_H

#include "dp-packet.h"
#include "netlink-protocol.h"
#include "openvswitch/packets.h"
#include "openvswitch/types.h"

struct dpif;
struct dpif_offload_sflow;

/* When offloading sample action, userspace creates a unique ID to map
 * sFlow action and tunnel info and passes this ID to datapath instead
 * of the sFlow info. Datapath will send this ID and sampled packet to
 * userspace. Using the ID, userspace can recover the sFlow info and send
 * sampled packet to the right sFlow monitoring host.
 */
struct dpif_sflow_attr {
    const struct nlattr *action; /* sFlow action */
    void *userdata;              /* struct user_action_cookie */
    size_t userdata_len;         /* struct user_action_cookie length */
    struct flow_tnl *tunnel;     /* tunnel info */
    ovs_u128 ufid;               /* flow ufid */
};

/* Parse the specific dpif message to sFlow. So OVS can process it. */
struct dpif_offload_sflow {
    struct dp_packet packet;    /* packet data */
    uint32_t iifindex;          /* input ifindex */
    const struct dpif_sflow_attr *attr;
};

struct dpif_offload_api {
    void (*init)(void);
    void (*uninit)(void);
    void (*sflow_recv_wait)(void);
    int (*sflow_recv)(struct dpif_offload_sflow *sflow);
};

void dpif_offload_sflow_recv_wait(const struct dpif *dpif);
int dpif_offload_sflow_recv(const struct dpif *dpif,
                            struct dpif_offload_sflow *sflow);

#ifdef __linux__
extern const struct dpif_offload_api dpif_offload_netlink;
#endif

#endif /* DPIF_OFFLOAD_PROVIDER_H */
