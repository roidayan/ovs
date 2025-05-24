/*
 * Copyright (c) 2014 Nicira, Inc.
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

#ifndef OVS_TNL_ROUTER_H
#define OVS_TNL_ROUTER_H 1

#include <sys/types.h>
#include <netinet/in.h>
#ifdef HAVE_NETLINK
#include <linux/rtnetlink.h>
#endif

#include "util.h"

#ifdef  __cplusplus
extern "C" {
#endif

enum {
#ifdef HAVE_NETLINK
    CLS_DEFAULT = RT_TABLE_MAIN,
#else
    CLS_DEFAULT = 254, /* RT_TABLE_MAIN */
#endif
    CLS_ALL = UINT_MAX,
};

#ifdef HAVE_NETLINK
static inline bool ovs_router_is_standard_table_id(uint32_t table_id)
{
    return !table_id
           || table_id == RT_TABLE_DEFAULT
           || table_id == RT_TABLE_MAIN
           || table_id == RT_TABLE_LOCAL;
}
#else
static inline bool ovs_router_is_standard_table_id(uint32_t table_id)
{
    return !table_id || table_id == CLS_DEFAULT;
}
#endif

bool ovs_router_lookup(uint32_t mark, const struct in6_addr *ip_dst,
                       char output_netdev[],
                       struct in6_addr *src, struct in6_addr *gw);
void ovs_router_init(void);
void ovs_router_insert(uint32_t table, uint32_t mark,
                       const struct in6_addr *ip_dst,
                       uint8_t plen, bool local,
                       const char output_netdev[], const struct in6_addr *gw,
                       const struct in6_addr *prefsrc);
void ovs_router_force_insert(uint32_t table, uint32_t mark,
                             const struct in6_addr *ip_dst,
                             uint8_t plen, bool local,
                             const char output_netdev[],
                             const struct in6_addr *gw,
                             const struct in6_addr *prefsrc);
void ovs_router_flush(void);

void ovs_router_disable_system_routing_table(void);

int ovs_router_get_netdev_source_address(const struct in6_addr *ip6_dst,
                                         const char netdev_name[],
                                         struct in6_addr *psrc);

#ifdef  __cplusplus
}
#endif

#endif
