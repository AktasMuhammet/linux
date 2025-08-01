/*
 * Copyright (c) 2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _MLX5_FS_
#define _MLX5_FS_

#include <linux/mlx5/driver.h>
#include <linux/mlx5/mlx5_ifc.h>

#define MLX5_FS_DEFAULT_FLOW_TAG 0x0

#define MLX5_SET_CFG(p, f, v) MLX5_SET(create_flow_group_in, p, f, v)

#define MLX5_RDMA_TRANSPORT_BYPASS_PRIO 16
#define MLX5_FS_MAX_POOL_SIZE BIT(30)

enum mlx5_flow_destination_type {
	MLX5_FLOW_DESTINATION_TYPE_NONE,
	MLX5_FLOW_DESTINATION_TYPE_VPORT,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE,
	MLX5_FLOW_DESTINATION_TYPE_TIR,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_SAMPLER,
	MLX5_FLOW_DESTINATION_TYPE_UPLINK,
	MLX5_FLOW_DESTINATION_TYPE_PORT,
	MLX5_FLOW_DESTINATION_TYPE_COUNTER,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM,
	MLX5_FLOW_DESTINATION_TYPE_RANGE,
	MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE,
};

enum {
	MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO	= 1 << 16,
	MLX5_FLOW_CONTEXT_ACTION_ENCRYPT	= 1 << 17,
	MLX5_FLOW_CONTEXT_ACTION_DECRYPT	= 1 << 18,
	MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_NS	= 1 << 19,
};

enum {
	MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT = BIT(0),
	MLX5_FLOW_TABLE_TUNNEL_EN_DECAP = BIT(1),
	MLX5_FLOW_TABLE_TERMINATION = BIT(2),
	MLX5_FLOW_TABLE_UNMANAGED = BIT(3),
	MLX5_FLOW_TABLE_OTHER_VPORT = BIT(4),
	MLX5_FLOW_TABLE_UPLINK_VPORT = BIT(5),
};

#define LEFTOVERS_RULE_NUM	 2
static inline void build_leftovers_ft_param(int *priority,
					    int *n_ent,
					    int *n_grp)
{
	*priority = 0; /* Priority of leftovers_prio-0 */
	*n_ent = LEFTOVERS_RULE_NUM;
	*n_grp = LEFTOVERS_RULE_NUM;
}

enum mlx5_flow_namespace_type {
	MLX5_FLOW_NAMESPACE_BYPASS,
	MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC,
	MLX5_FLOW_NAMESPACE_LAG,
	MLX5_FLOW_NAMESPACE_OFFLOADS,
	MLX5_FLOW_NAMESPACE_ETHTOOL,
	MLX5_FLOW_NAMESPACE_KERNEL,
	MLX5_FLOW_NAMESPACE_LEFTOVERS,
	MLX5_FLOW_NAMESPACE_ANCHOR,
	MLX5_FLOW_NAMESPACE_FDB_BYPASS,
	MLX5_FLOW_NAMESPACE_FDB,
	MLX5_FLOW_NAMESPACE_ESW_EGRESS,
	MLX5_FLOW_NAMESPACE_ESW_INGRESS,
	MLX5_FLOW_NAMESPACE_SNIFFER_RX,
	MLX5_FLOW_NAMESPACE_SNIFFER_TX,
	MLX5_FLOW_NAMESPACE_EGRESS,
	MLX5_FLOW_NAMESPACE_EGRESS_IPSEC,
	MLX5_FLOW_NAMESPACE_EGRESS_MACSEC,
	MLX5_FLOW_NAMESPACE_RDMA_RX,
	MLX5_FLOW_NAMESPACE_RDMA_RX_KERNEL,
	MLX5_FLOW_NAMESPACE_RDMA_TX,
	MLX5_FLOW_NAMESPACE_PORT_SEL,
	MLX5_FLOW_NAMESPACE_RDMA_RX_COUNTERS,
	MLX5_FLOW_NAMESPACE_RDMA_TX_COUNTERS,
	MLX5_FLOW_NAMESPACE_RDMA_RX_IPSEC,
	MLX5_FLOW_NAMESPACE_RDMA_TX_IPSEC,
	MLX5_FLOW_NAMESPACE_RDMA_RX_MACSEC,
	MLX5_FLOW_NAMESPACE_RDMA_TX_MACSEC,
	MLX5_FLOW_NAMESPACE_RDMA_TRANSPORT_RX,
	MLX5_FLOW_NAMESPACE_RDMA_TRANSPORT_TX,
};

enum {
	FDB_BYPASS_PATH,
	FDB_CRYPTO_INGRESS,
	FDB_TC_OFFLOAD,
	FDB_FT_OFFLOAD,
	FDB_TC_MISS,
	FDB_BR_OFFLOAD,
	FDB_SLOW_PATH,
	FDB_CRYPTO_EGRESS,
	FDB_PER_VPORT,
};

struct mlx5_pkt_reformat;
struct mlx5_modify_hdr;
struct mlx5_flow_definer;
struct mlx5_flow_table;
struct mlx5_flow_group;
struct mlx5_flow_namespace;
struct mlx5_flow_handle;

enum {
	FLOW_CONTEXT_HAS_TAG = BIT(0),
	FLOW_CONTEXT_UPLINK_HAIRPIN_EN = BIT(1),
};

struct mlx5_flow_context {
	u32 flags;
	u32 flow_tag;
	u32 flow_source;
};

struct mlx5_flow_spec {
	u8   match_criteria_enable;
	u32  match_criteria[MLX5_ST_SZ_DW(fte_match_param)];
	u32  match_value[MLX5_ST_SZ_DW(fte_match_param)];
	struct mlx5_flow_context flow_context;
};

enum {
	MLX5_FLOW_DEST_VPORT_VHCA_ID      = BIT(0),
	MLX5_FLOW_DEST_VPORT_REFORMAT_ID  = BIT(1),
};

enum mlx5_flow_dest_range_field {
	MLX5_FLOW_DEST_RANGE_FIELD_PKT_LEN = 0,
};

struct mlx5_flow_destination {
	enum mlx5_flow_destination_type	type;
	union {
		u32			tir_num;
		u32			ft_num;
		struct mlx5_flow_table	*ft;
		struct mlx5_fc          *counter;
		struct {
			u16		num;
			u16		vhca_id;
			struct mlx5_pkt_reformat *pkt_reformat;
			u8		flags;
		} vport;
		struct {
			struct mlx5_flow_table         *hit_ft;
			struct mlx5_flow_table         *miss_ft;
			enum mlx5_flow_dest_range_field field;
			u32                             min;
			u32                             max;
		} range;
		u32			sampler_id;
	};
};

struct mod_hdr_tbl {
	struct mutex lock; /* protects hlist */
	DECLARE_HASHTABLE(hlist, 8);
};

struct mlx5_flow_namespace *
mlx5_get_fdb_sub_ns(struct mlx5_core_dev *dev, int n);
struct mlx5_flow_namespace *
mlx5_get_flow_namespace(struct mlx5_core_dev *dev,
			enum mlx5_flow_namespace_type type);
struct mlx5_flow_namespace *
mlx5_get_flow_vport_namespace(struct mlx5_core_dev *dev,
			      enum mlx5_flow_namespace_type type,
			      int vport_idx);

struct mlx5_flow_table_attr {
	int prio;
	int max_fte;
	u32 level;
	u32 flags;
	u16 uid;
	u16 vport;
	struct mlx5_flow_table *next_ft;

	struct {
		int max_num_groups;
		int num_reserved_entries;
	} autogroup;
};

struct mlx5_flow_table *
mlx5_create_flow_table(struct mlx5_flow_namespace *ns,
		       struct mlx5_flow_table_attr *ft_attr);

struct mlx5_flow_table *
mlx5_create_auto_grouped_flow_table(struct mlx5_flow_namespace *ns,
				    struct mlx5_flow_table_attr *ft_attr);

struct mlx5_flow_table *
mlx5_create_vport_flow_table(struct mlx5_flow_namespace *ns,
			     struct mlx5_flow_table_attr *ft_attr, u16 vport);
struct mlx5_flow_table *mlx5_create_lag_demux_flow_table(
					       struct mlx5_flow_namespace *ns,
					       int prio, u32 level);
int mlx5_destroy_flow_table(struct mlx5_flow_table *ft);

/* inbox should be set with the following values:
 * start_flow_index
 * end_flow_index
 * match_criteria_enable
 * match_criteria
 */
struct mlx5_flow_group *
mlx5_create_flow_group(struct mlx5_flow_table *ft, u32 *in);
void mlx5_destroy_flow_group(struct mlx5_flow_group *fg);

struct mlx5_exe_aso {
	u32 object_id;
	int base_id;
	u8 type;
	u8 return_reg_id;
	union {
		u32 ctrl_data;
		struct {
			u8 meter_idx;
			u8 init_color;
		} flow_meter;
	};
};

struct mlx5_fs_vlan {
        u16 ethtype;
        u16 vid;
        u8  prio;
};

#define MLX5_FS_VLAN_DEPTH	2

enum {
	FLOW_ACT_NO_APPEND = BIT(0),
	FLOW_ACT_IGNORE_FLOW_LEVEL = BIT(1),
};

struct mlx5_flow_act {
	u32 action;
	struct mlx5_modify_hdr  *modify_hdr;
	struct mlx5_pkt_reformat *pkt_reformat;
	struct mlx5_flow_act_crypto_params {
		u8 type;
		u32 obj_id;
	} crypto;
	u32 flags;
	struct mlx5_fs_vlan vlan[MLX5_FS_VLAN_DEPTH];
	struct ib_counters *counters;
	struct mlx5_flow_group *fg;
	struct mlx5_exe_aso exe_aso;
};

#define MLX5_DECLARE_FLOW_ACT(name) \
	struct mlx5_flow_act name = { .action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,\
				      .flags =  0, }

/* Single destination per rule.
 * Group ID is implied by the match criteria.
 */
struct mlx5_flow_handle *
mlx5_add_flow_rules(struct mlx5_flow_table *ft,
		    const struct mlx5_flow_spec *spec,
		    struct mlx5_flow_act *flow_act,
		    struct mlx5_flow_destination *dest,
		    int num_dest);
void mlx5_del_flow_rules(struct mlx5_flow_handle *fr);

int mlx5_modify_rule_destination(struct mlx5_flow_handle *handler,
				 struct mlx5_flow_destination *new_dest,
				 struct mlx5_flow_destination *old_dest);

struct mlx5_fc *mlx5_fc_create(struct mlx5_core_dev *dev, bool aging);

void mlx5_fc_destroy(struct mlx5_core_dev *dev, struct mlx5_fc *counter);
struct mlx5_fc *mlx5_fc_local_create(u32 counter_id, u32 offset, u32 bulk_size);
void mlx5_fc_local_destroy(struct mlx5_fc *counter);
u64 mlx5_fc_query_lastuse(struct mlx5_fc *counter);
void mlx5_fc_query_cached(struct mlx5_fc *counter,
			  u64 *bytes, u64 *packets, u64 *lastuse);
void mlx5_fc_query_cached_raw(struct mlx5_fc *counter,
			      u64 *bytes, u64 *packets, u64 *lastuse);
int mlx5_fc_query(struct mlx5_core_dev *dev, struct mlx5_fc *counter,
		  u64 *packets, u64 *bytes);
u32 mlx5_fc_id(struct mlx5_fc *counter);

int mlx5_fs_add_rx_underlay_qpn(struct mlx5_core_dev *dev, u32 underlay_qpn);
int mlx5_fs_remove_rx_underlay_qpn(struct mlx5_core_dev *dev, u32 underlay_qpn);

struct mlx5_modify_hdr *mlx5_modify_header_alloc(struct mlx5_core_dev *dev,
						 u8 ns_type, u8 num_actions,
						 void *modify_actions);
void mlx5_modify_header_dealloc(struct mlx5_core_dev *dev,
				struct mlx5_modify_hdr *modify_hdr);
struct mlx5_flow_definer *
mlx5_create_match_definer(struct mlx5_core_dev *dev,
			  enum mlx5_flow_namespace_type ns_type, u16 format_id,
			  u32 *match_mask);
void mlx5_destroy_match_definer(struct mlx5_core_dev *dev,
				struct mlx5_flow_definer *definer);
int mlx5_get_match_definer_id(struct mlx5_flow_definer *definer);

struct mlx5_pkt_reformat_params {
	int type;
	u8 param_0;
	u8 param_1;
	size_t size;
	void *data;
};

struct mlx5_pkt_reformat *mlx5_packet_reformat_alloc(struct mlx5_core_dev *dev,
						     struct mlx5_pkt_reformat_params *params,
						     enum mlx5_flow_namespace_type ns_type);
void mlx5_packet_reformat_dealloc(struct mlx5_core_dev *dev,
				  struct mlx5_pkt_reformat *reformat);

u32 mlx5_flow_table_id(struct mlx5_flow_table *ft);

struct mlx5_flow_root_namespace *
mlx5_get_root_namespace(struct mlx5_core_dev *dev, enum mlx5_flow_namespace_type ns_type);
#endif
