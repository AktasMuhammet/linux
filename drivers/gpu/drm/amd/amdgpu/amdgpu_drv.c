/*
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <drm/amdgpu_drm.h>
#include <drm/clients/drm_client_setup.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_ttm.h>
#include <drm/drm_gem.h>
#include <drm/drm_managed.h>
#include <drm/drm_pciids.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <linux/cc_platform.h>
#include <linux/dynamic_debug.h>
#include <linux/module.h>
#include <linux/mmu_notifier.h>
#include <linux/pm_runtime.h>
#include <linux/suspend.h>
#include <linux/vga_switcheroo.h>

#include "amdgpu.h"
#include "amdgpu_amdkfd.h"
#include "amdgpu_dma_buf.h"
#include "amdgpu_drv.h"
#include "amdgpu_fdinfo.h"
#include "amdgpu_irq.h"
#include "amdgpu_psp.h"
#include "amdgpu_ras.h"
#include "amdgpu_reset.h"
#include "amdgpu_sched.h"
#include "amdgpu_xgmi.h"
#include "amdgpu_userq.h"
#include "amdgpu_userq_fence.h"
#include "../amdxcp/amdgpu_xcp_drv.h"

/*
 * KMS wrapper.
 * - 3.0.0 - initial driver
 * - 3.1.0 - allow reading more status registers (GRBM, SRBM, SDMA, CP)
 * - 3.2.0 - GFX8: Uses EOP_TC_WB_ACTION_EN, so UMDs don't have to do the same
 *           at the end of IBs.
 * - 3.3.0 - Add VM support for UVD on supported hardware.
 * - 3.4.0 - Add AMDGPU_INFO_NUM_EVICTIONS.
 * - 3.5.0 - Add support for new UVD_NO_OP register.
 * - 3.6.0 - kmd involves use CONTEXT_CONTROL in ring buffer.
 * - 3.7.0 - Add support for VCE clock list packet
 * - 3.8.0 - Add support raster config init in the kernel
 * - 3.9.0 - Add support for memory query info about VRAM and GTT.
 * - 3.10.0 - Add support for new fences ioctl, new gem ioctl flags
 * - 3.11.0 - Add support for sensor query info (clocks, temp, etc).
 * - 3.12.0 - Add query for double offchip LDS buffers
 * - 3.13.0 - Add PRT support
 * - 3.14.0 - Fix race in amdgpu_ctx_get_fence() and note new functionality
 * - 3.15.0 - Export more gpu info for gfx9
 * - 3.16.0 - Add reserved vmid support
 * - 3.17.0 - Add AMDGPU_NUM_VRAM_CPU_PAGE_FAULTS.
 * - 3.18.0 - Export gpu always on cu bitmap
 * - 3.19.0 - Add support for UVD MJPEG decode
 * - 3.20.0 - Add support for local BOs
 * - 3.21.0 - Add DRM_AMDGPU_FENCE_TO_HANDLE ioctl
 * - 3.22.0 - Add DRM_AMDGPU_SCHED ioctl
 * - 3.23.0 - Add query for VRAM lost counter
 * - 3.24.0 - Add high priority compute support for gfx9
 * - 3.25.0 - Add support for sensor query info (stable pstate sclk/mclk).
 * - 3.26.0 - GFX9: Process AMDGPU_IB_FLAG_TC_WB_NOT_INVALIDATE.
 * - 3.27.0 - Add new chunk to AMDGPU_CS to enable BO_LIST creation.
 * - 3.28.0 - Add AMDGPU_CHUNK_ID_SCHEDULED_DEPENDENCIES
 * - 3.29.0 - Add AMDGPU_IB_FLAG_RESET_GDS_MAX_WAVE_ID
 * - 3.30.0 - Add AMDGPU_SCHED_OP_CONTEXT_PRIORITY_OVERRIDE.
 * - 3.31.0 - Add support for per-flip tiling attribute changes with DC
 * - 3.32.0 - Add syncobj timeline support to AMDGPU_CS.
 * - 3.33.0 - Fixes for GDS ENOMEM failures in AMDGPU_CS.
 * - 3.34.0 - Non-DC can flip correctly between buffers with different pitches
 * - 3.35.0 - Add drm_amdgpu_info_device::tcc_disabled_mask
 * - 3.36.0 - Allow reading more status registers on si/cik
 * - 3.37.0 - L2 is invalidated before SDMA IBs, needed for correctness
 * - 3.38.0 - Add AMDGPU_IB_FLAG_EMIT_MEM_SYNC
 * - 3.39.0 - DMABUF implicit sync does a full pipeline sync
 * - 3.40.0 - Add AMDGPU_IDS_FLAGS_TMZ
 * - 3.41.0 - Add video codec query
 * - 3.42.0 - Add 16bpc fixed point display support
 * - 3.43.0 - Add device hot plug/unplug support
 * - 3.44.0 - DCN3 supports DCC independent block settings: !64B && 128B, 64B && 128B
 * - 3.45.0 - Add context ioctl stable pstate interface
 * - 3.46.0 - To enable hot plug amdgpu tests in libdrm
 * - 3.47.0 - Add AMDGPU_GEM_CREATE_DISCARDABLE and AMDGPU_VM_NOALLOC flags
 * - 3.48.0 - Add IP discovery version info to HW INFO
 * - 3.49.0 - Add gang submit into CS IOCTL
 * - 3.50.0 - Update AMDGPU_INFO_DEV_INFO IOCTL for minimum engine and memory clock
 *            Update AMDGPU_INFO_SENSOR IOCTL for PEAK_PSTATE engine and memory clock
 *   3.51.0 - Return the PCIe gen and lanes from the INFO ioctl
 *   3.52.0 - Add AMDGPU_IDS_FLAGS_CONFORMANT_TRUNC_COORD, add device_info fields:
 *            tcp_cache_size, num_sqc_per_wgp, sqc_data_cache_size, sqc_inst_cache_size,
 *            gl1c_cache_size, gl2c_cache_size, mall_size, enabled_rb_pipes_mask_hi
 *   3.53.0 - Support for GFX11 CP GFX shadowing
 *   3.54.0 - Add AMDGPU_CTX_QUERY2_FLAGS_RESET_IN_PROGRESS support
 * - 3.55.0 - Add AMDGPU_INFO_GPUVM_FAULT query
 * - 3.56.0 - Update IB start address and size alignment for decode and encode
 * - 3.57.0 - Compute tunneling on GFX10+
 * - 3.58.0 - Add GFX12 DCC support
 * - 3.59.0 - Cleared VRAM
 * - 3.60.0 - Add AMDGPU_TILING_GFX12_DCC_WRITE_COMPRESS_DISABLE (Vulkan requirement)
 * - 3.61.0 - Contains fix for RV/PCO compute queues
 * - 3.62.0 - Add AMDGPU_IDS_FLAGS_MODE_PF, AMDGPU_IDS_FLAGS_MODE_VF & AMDGPU_IDS_FLAGS_MODE_PT
 * - 3.63.0 - GFX12 display DCC supports 256B max compressed block size
 * - 3.64.0 - Userq IP support query
 */
#define KMS_DRIVER_MAJOR	3
#define KMS_DRIVER_MINOR	64
#define KMS_DRIVER_PATCHLEVEL	0

/*
 * amdgpu.debug module options. Are all disabled by default
 */
enum AMDGPU_DEBUG_MASK {
	AMDGPU_DEBUG_VM = BIT(0),
	AMDGPU_DEBUG_LARGEBAR = BIT(1),
	AMDGPU_DEBUG_DISABLE_GPU_SOFT_RECOVERY = BIT(2),
	AMDGPU_DEBUG_USE_VRAM_FW_BUF = BIT(3),
	AMDGPU_DEBUG_ENABLE_RAS_ACA = BIT(4),
	AMDGPU_DEBUG_ENABLE_EXP_RESETS = BIT(5),
	AMDGPU_DEBUG_DISABLE_GPU_RING_RESET = BIT(6),
	AMDGPU_DEBUG_SMU_POOL = BIT(7),
	AMDGPU_DEBUG_VM_USERPTR = BIT(8),
	AMDGPU_DEBUG_DISABLE_RAS_CE_LOG = BIT(9)
};

unsigned int amdgpu_vram_limit = UINT_MAX;
int amdgpu_vis_vram_limit;
int amdgpu_gart_size = -1; /* auto */
int amdgpu_gtt_size = -1; /* auto */
int amdgpu_moverate = -1; /* auto */
int amdgpu_audio = -1;
int amdgpu_disp_priority;
int amdgpu_hw_i2c;
int amdgpu_pcie_gen2 = -1;
int amdgpu_msi = -1;
char amdgpu_lockup_timeout[AMDGPU_MAX_TIMEOUT_PARAM_LENGTH];
int amdgpu_dpm = -1;
int amdgpu_fw_load_type = -1;
int amdgpu_aspm = -1;
int amdgpu_runtime_pm = -1;
uint amdgpu_ip_block_mask = 0xffffffff;
int amdgpu_bapm = -1;
int amdgpu_deep_color;
int amdgpu_vm_size = -1;
int amdgpu_vm_fragment_size = -1;
int amdgpu_vm_block_size = -1;
int amdgpu_vm_fault_stop;
int amdgpu_vm_update_mode = -1;
int amdgpu_exp_hw_support;
int amdgpu_dc = -1;
int amdgpu_sched_jobs = 32;
int amdgpu_sched_hw_submission = 2;
uint amdgpu_pcie_gen_cap;
uint amdgpu_pcie_lane_cap;
u64 amdgpu_cg_mask = 0xffffffffffffffff;
uint amdgpu_pg_mask = 0xffffffff;
uint amdgpu_sdma_phase_quantum = 32;
char *amdgpu_disable_cu;
char *amdgpu_virtual_display;
int amdgpu_enforce_isolation = -1;
int amdgpu_modeset = -1;

/* Specifies the default granularity for SVM, used in buffer
 * migration and restoration of backing memory when handling
 * recoverable page faults.
 *
 * The value is given as log(numPages(buffer)); for a 2 MiB
 * buffer it computes to be 9
 */
uint amdgpu_svm_default_granularity = 9;

/*
 * OverDrive(bit 14) disabled by default
 * GFX DCS(bit 19) disabled by default
 */
uint amdgpu_pp_feature_mask = 0xfff7bfff;
uint amdgpu_force_long_training;
int amdgpu_lbpw = -1;
int amdgpu_compute_multipipe = -1;
int amdgpu_gpu_recovery = -1; /* auto */
int amdgpu_emu_mode;
uint amdgpu_smu_memory_pool_size;
int amdgpu_smu_pptable_id = -1;
/*
 * FBC (bit 0) disabled by default
 * MULTI_MON_PP_MCLK_SWITCH (bit 1) enabled by default
 *   - With this, for multiple monitors in sync(e.g. with the same model),
 *     mclk switching will be allowed. And the mclk will be not foced to the
 *     highest. That helps saving some idle power.
 * DISABLE_FRACTIONAL_PWM (bit 2) disabled by default
 * PSR (bit 3) disabled by default
 * EDP NO POWER SEQUENCING (bit 4) disabled by default
 */
uint amdgpu_dc_feature_mask = 2;
uint amdgpu_dc_debug_mask;
uint amdgpu_dc_visual_confirm;
int amdgpu_async_gfx_ring = 1;
int amdgpu_mcbp = -1;
int amdgpu_discovery = -1;
int amdgpu_mes;
int amdgpu_mes_log_enable = 0;
int amdgpu_mes_kiq;
int amdgpu_uni_mes = 1;
int amdgpu_noretry = -1;
int amdgpu_force_asic_type = -1;
int amdgpu_tmz = -1; /* auto */
uint amdgpu_freesync_vid_mode;
int amdgpu_reset_method = -1; /* auto */
int amdgpu_num_kcq = -1;
int amdgpu_smartshift_bias;
int amdgpu_use_xgmi_p2p = 1;
int amdgpu_vcnfw_log;
int amdgpu_sg_display = -1; /* auto */
int amdgpu_user_partt_mode = AMDGPU_AUTO_COMPUTE_PARTITION_MODE;
int amdgpu_umsch_mm;
int amdgpu_seamless = -1; /* auto */
uint amdgpu_debug_mask;
int amdgpu_agp = -1; /* auto */
int amdgpu_wbrf = -1;
int amdgpu_damage_clips = -1; /* auto */
int amdgpu_umsch_mm_fwlog;
int amdgpu_rebar = -1; /* auto */
int amdgpu_user_queue = -1;

DECLARE_DYNDBG_CLASSMAP(drm_debug_classes, DD_CLASS_TYPE_DISJOINT_BITS, 0,
			"DRM_UT_CORE",
			"DRM_UT_DRIVER",
			"DRM_UT_KMS",
			"DRM_UT_PRIME",
			"DRM_UT_ATOMIC",
			"DRM_UT_VBL",
			"DRM_UT_STATE",
			"DRM_UT_LEASE",
			"DRM_UT_DP",
			"DRM_UT_DRMRES");

struct amdgpu_mgpu_info mgpu_info = {
	.mutex = __MUTEX_INITIALIZER(mgpu_info.mutex),
};
int amdgpu_ras_enable = -1;
uint amdgpu_ras_mask = 0xffffffff;
int amdgpu_bad_page_threshold = -1;
struct amdgpu_watchdog_timer amdgpu_watchdog_timer = {
	.timeout_fatal_disable = false,
	.period = 0x0, /* default to 0x0 (timeout disable) */
};

/**
 * DOC: vramlimit (int)
 * Restrict the total amount of VRAM in MiB for testing.  The default is 0 (Use full VRAM).
 */
MODULE_PARM_DESC(vramlimit, "Restrict VRAM for testing, in megabytes");
module_param_named(vramlimit, amdgpu_vram_limit, int, 0600);

/**
 * DOC: vis_vramlimit (int)
 * Restrict the amount of CPU visible VRAM in MiB for testing.  The default is 0 (Use full CPU visible VRAM).
 */
MODULE_PARM_DESC(vis_vramlimit, "Restrict visible VRAM for testing, in megabytes");
module_param_named(vis_vramlimit, amdgpu_vis_vram_limit, int, 0444);

/**
 * DOC: gartsize (uint)
 * Restrict the size of GART (for kernel use) in Mib (32, 64, etc.) for testing.
 * The default is -1 (The size depends on asic).
 */
MODULE_PARM_DESC(gartsize, "Size of kernel GART to setup in megabytes (32, 64, etc., -1=auto)");
module_param_named(gartsize, amdgpu_gart_size, uint, 0600);

/**
 * DOC: gttsize (int)
 * Restrict the size of GTT domain (for userspace use) in MiB for testing.
 * The default is -1 (Use value specified by TTM).
 * This parameter is deprecated and will be removed in the future.
 */
MODULE_PARM_DESC(gttsize, "Size of the GTT userspace domain in megabytes (-1 = auto)");
module_param_named(gttsize, amdgpu_gtt_size, int, 0600);

/**
 * DOC: moverate (int)
 * Set maximum buffer migration rate in MB/s. The default is -1 (8 MB/s).
 */
MODULE_PARM_DESC(moverate, "Maximum buffer migration rate in MB/s. (32, 64, etc., -1=auto, 0=1=disabled)");
module_param_named(moverate, amdgpu_moverate, int, 0600);

/**
 * DOC: audio (int)
 * Set HDMI/DPAudio. Only affects non-DC display handling. The default is -1 (Enabled), set 0 to disabled it.
 */
MODULE_PARM_DESC(audio, "Audio enable (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(audio, amdgpu_audio, int, 0444);

/**
 * DOC: disp_priority (int)
 * Set display Priority (1 = normal, 2 = high). Only affects non-DC display handling. The default is 0 (auto).
 */
MODULE_PARM_DESC(disp_priority, "Display Priority (0 = auto, 1 = normal, 2 = high)");
module_param_named(disp_priority, amdgpu_disp_priority, int, 0444);

/**
 * DOC: hw_i2c (int)
 * To enable hw i2c engine. Only affects non-DC display handling. The default is 0 (Disabled).
 */
MODULE_PARM_DESC(hw_i2c, "hw i2c engine enable (0 = disable)");
module_param_named(hw_i2c, amdgpu_hw_i2c, int, 0444);

/**
 * DOC: pcie_gen2 (int)
 * To disable PCIE Gen2/3 mode (0 = disable, 1 = enable). The default is -1 (auto, enabled).
 */
MODULE_PARM_DESC(pcie_gen2, "PCIE Gen2 mode (-1 = auto, 0 = disable, 1 = enable)");
module_param_named(pcie_gen2, amdgpu_pcie_gen2, int, 0444);

/**
 * DOC: msi (int)
 * To disable Message Signaled Interrupts (MSI) functionality (1 = enable, 0 = disable). The default is -1 (auto, enabled).
 */
MODULE_PARM_DESC(msi, "MSI support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(msi, amdgpu_msi, int, 0444);

/**
 * DOC: svm_default_granularity (uint)
 * Used in buffer migration and handling of recoverable page faults
 */
MODULE_PARM_DESC(svm_default_granularity, "SVM's default granularity in log(2^Pages), default 9 = 2^9 = 2 MiB");
module_param_named(svm_default_granularity, amdgpu_svm_default_granularity, uint, 0644);

/**
 * DOC: lockup_timeout (string)
 * Set GPU scheduler timeout value in ms.
 *
 * The format can be [Non-Compute] or [GFX,Compute,SDMA,Video]. That is there can be one or
 * multiple values specified. 0 and negative values are invalidated. They will be adjusted
 * to the default timeout.
 *
 * - With one value specified, the setting will apply to all non-compute jobs.
 * - With multiple values specified, the first one will be for GFX.
 *   The second one is for Compute. The third and fourth ones are
 *   for SDMA and Video.
 *
 * By default(with no lockup_timeout settings), the timeout for all jobs is 10000.
 */
MODULE_PARM_DESC(lockup_timeout,
		 "GPU lockup timeout in ms (default: 10000 for all jobs. "
		 "0: keep default value. negative: infinity timeout), format: for bare metal [Non-Compute] or [GFX,Compute,SDMA,Video]; "
		 "for passthrough or sriov [all jobs] or [GFX,Compute,SDMA,Video].");
module_param_string(lockup_timeout, amdgpu_lockup_timeout, sizeof(amdgpu_lockup_timeout), 0444);

/**
 * DOC: dpm (int)
 * Override for dynamic power management setting
 * (0 = disable, 1 = enable)
 * The default is -1 (auto).
 */
MODULE_PARM_DESC(dpm, "DPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(dpm, amdgpu_dpm, int, 0444);

/**
 * DOC: fw_load_type (int)
 * Set different firmware loading type for debugging, if supported.
 * Set to 0 to force direct loading if supported by the ASIC.  Set
 * to -1 to select the default loading mode for the ASIC, as defined
 * by the driver.  The default is -1 (auto).
 */
MODULE_PARM_DESC(fw_load_type, "firmware loading type (3 = rlc backdoor autoload if supported, 2 = smu load if supported, 1 = psp load, 0 = force direct if supported, -1 = auto)");
module_param_named(fw_load_type, amdgpu_fw_load_type, int, 0444);

/**
 * DOC: aspm (int)
 * To disable ASPM (1 = enable, 0 = disable). The default is -1 (auto, enabled).
 */
MODULE_PARM_DESC(aspm, "ASPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(aspm, amdgpu_aspm, int, 0444);

/**
 * DOC: runpm (int)
 * Override for runtime power management control for dGPUs. The amdgpu driver can dynamically power down
 * the dGPUs when they are idle if supported. The default is -1 (auto enable).
 * Setting the value to 0 disables this functionality.
 * Setting the value to -2 is auto enabled with power down when displays are attached.
 */
MODULE_PARM_DESC(runpm, "PX runtime pm (2 = force enable with BAMACO, 1 = force enable with BACO, 0 = disable, -1 = auto, -2 = auto with displays)");
module_param_named(runpm, amdgpu_runtime_pm, int, 0444);

/**
 * DOC: ip_block_mask (uint)
 * Override what IP blocks are enabled on the GPU. Each GPU is a collection of IP blocks (gfx, display, video, etc.).
 * Use this parameter to disable specific blocks. Note that the IP blocks do not have a fixed index. Some asics may not have
 * some IPs or may include multiple instances of an IP so the ordering various from asic to asic. See the driver output in
 * the kernel log for the list of IPs on the asic. The default is 0xffffffff (enable all blocks on a device).
 */
MODULE_PARM_DESC(ip_block_mask, "IP Block Mask (all blocks enabled (default))");
module_param_named_unsafe(ip_block_mask, amdgpu_ip_block_mask, uint, 0444);

/**
 * DOC: bapm (int)
 * Bidirectional Application Power Management (BAPM) used to dynamically share TDP between CPU and GPU. Set value 0 to disable it.
 * The default -1 (auto, enabled)
 */
MODULE_PARM_DESC(bapm, "BAPM support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(bapm, amdgpu_bapm, int, 0444);

/**
 * DOC: deep_color (int)
 * Set 1 to enable Deep Color support. Only affects non-DC display handling. The default is 0 (disabled).
 */
MODULE_PARM_DESC(deep_color, "Deep Color support (1 = enable, 0 = disable (default))");
module_param_named(deep_color, amdgpu_deep_color, int, 0444);

/**
 * DOC: vm_size (int)
 * Override the size of the GPU's per client virtual address space in GiB.  The default is -1 (automatic for each asic).
 */
MODULE_PARM_DESC(vm_size, "VM address space size in gigabytes (default 64GB)");
module_param_named(vm_size, amdgpu_vm_size, int, 0444);

/**
 * DOC: vm_fragment_size (int)
 * Override VM fragment size in bits (4, 5, etc. 4 = 64K, 9 = 2M). The default is -1 (automatic for each asic).
 */
MODULE_PARM_DESC(vm_fragment_size, "VM fragment size in bits (4, 5, etc. 4 = 64K (default), Max 9 = 2M)");
module_param_named(vm_fragment_size, amdgpu_vm_fragment_size, int, 0444);

/**
 * DOC: vm_block_size (int)
 * Override VM page table size in bits (default depending on vm_size and hw setup). The default is -1 (automatic for each asic).
 */
MODULE_PARM_DESC(vm_block_size, "VM page table size in bits (default depending on vm_size)");
module_param_named(vm_block_size, amdgpu_vm_block_size, int, 0444);

/**
 * DOC: vm_fault_stop (int)
 * Stop on VM fault for debugging (0 = never, 1 = print first, 2 = always). The default is 0 (No stop).
 */
MODULE_PARM_DESC(vm_fault_stop, "Stop on VM fault (0 = never (default), 1 = print first, 2 = always)");
module_param_named(vm_fault_stop, amdgpu_vm_fault_stop, int, 0444);

/**
 * DOC: vm_update_mode (int)
 * Override VM update mode. VM updated by using CPU (0 = never, 1 = Graphics only, 2 = Compute only, 3 = Both). The default
 * is -1 (Only in large BAR(LB) systems Compute VM tables will be updated by CPU, otherwise 0, never).
 */
MODULE_PARM_DESC(vm_update_mode, "VM update using CPU (0 = never (default except for large BAR(LB)), 1 = Graphics only, 2 = Compute only (default for LB), 3 = Both");
module_param_named(vm_update_mode, amdgpu_vm_update_mode, int, 0444);

/**
 * DOC: exp_hw_support (int)
 * Enable experimental hw support (1 = enable). The default is 0 (disabled).
 */
MODULE_PARM_DESC(exp_hw_support, "experimental hw support (1 = enable, 0 = disable (default))");
module_param_named_unsafe(exp_hw_support, amdgpu_exp_hw_support, int, 0444);

/**
 * DOC: dc (int)
 * Disable/Enable Display Core driver for debugging (1 = enable, 0 = disable). The default is -1 (automatic for each asic).
 */
MODULE_PARM_DESC(dc, "Display Core driver (1 = enable, 0 = disable, -1 = auto (default))");
module_param_named(dc, amdgpu_dc, int, 0444);

/**
 * DOC: sched_jobs (int)
 * Override the max number of jobs supported in the sw queue. The default is 32.
 */
MODULE_PARM_DESC(sched_jobs, "the max number of jobs supported in the sw queue (default 32)");
module_param_named(sched_jobs, amdgpu_sched_jobs, int, 0444);

/**
 * DOC: sched_hw_submission (int)
 * Override the max number of HW submissions. The default is 2.
 */
MODULE_PARM_DESC(sched_hw_submission, "the max number of HW submissions (default 2)");
module_param_named(sched_hw_submission, amdgpu_sched_hw_submission, int, 0444);

/**
 * DOC: ppfeaturemask (hexint)
 * Override power features enabled. See enum PP_FEATURE_MASK in drivers/gpu/drm/amd/include/amd_shared.h.
 * The default is the current set of stable power features.
 */
MODULE_PARM_DESC(ppfeaturemask, "all power features enabled (default))");
module_param_named(ppfeaturemask, amdgpu_pp_feature_mask, hexint, 0444);

/**
 * DOC: forcelongtraining (uint)
 * Force long memory training in resume.
 * The default is zero, indicates short training in resume.
 */
MODULE_PARM_DESC(forcelongtraining, "force memory long training");
module_param_named(forcelongtraining, amdgpu_force_long_training, uint, 0444);

/**
 * DOC: pcie_gen_cap (uint)
 * Override PCIE gen speed capabilities. See the CAIL flags in drivers/gpu/drm/amd/include/amd_pcie.h.
 * The default is 0 (automatic for each asic).
 */
MODULE_PARM_DESC(pcie_gen_cap, "PCIE Gen Caps (0: autodetect (default))");
module_param_named(pcie_gen_cap, amdgpu_pcie_gen_cap, uint, 0444);

/**
 * DOC: pcie_lane_cap (uint)
 * Override PCIE lanes capabilities. See the CAIL flags in drivers/gpu/drm/amd/include/amd_pcie.h.
 * The default is 0 (automatic for each asic).
 */
MODULE_PARM_DESC(pcie_lane_cap, "PCIE Lane Caps (0: autodetect (default))");
module_param_named(pcie_lane_cap, amdgpu_pcie_lane_cap, uint, 0444);

/**
 * DOC: cg_mask (ullong)
 * Override Clockgating features enabled on GPU (0 = disable clock gating). See the AMD_CG_SUPPORT flags in
 * drivers/gpu/drm/amd/include/amd_shared.h. The default is 0xffffffffffffffff (all enabled).
 */
MODULE_PARM_DESC(cg_mask, "Clockgating flags mask (0 = disable clock gating)");
module_param_named(cg_mask, amdgpu_cg_mask, ullong, 0444);

/**
 * DOC: pg_mask (uint)
 * Override Powergating features enabled on GPU (0 = disable power gating). See the AMD_PG_SUPPORT flags in
 * drivers/gpu/drm/amd/include/amd_shared.h. The default is 0xffffffff (all enabled).
 */
MODULE_PARM_DESC(pg_mask, "Powergating flags mask (0 = disable power gating)");
module_param_named(pg_mask, amdgpu_pg_mask, uint, 0444);

/**
 * DOC: sdma_phase_quantum (uint)
 * Override SDMA context switch phase quantum (x 1K GPU clock cycles, 0 = no change). The default is 32.
 */
MODULE_PARM_DESC(sdma_phase_quantum, "SDMA context switch phase quantum (x 1K GPU clock cycles, 0 = no change (default 32))");
module_param_named(sdma_phase_quantum, amdgpu_sdma_phase_quantum, uint, 0444);

/**
 * DOC: disable_cu (charp)
 * Set to disable CUs (It's set like se.sh.cu,...). The default is NULL.
 */
MODULE_PARM_DESC(disable_cu, "Disable CUs (se.sh.cu,...)");
module_param_named(disable_cu, amdgpu_disable_cu, charp, 0444);

/**
 * DOC: virtual_display (charp)
 * Set to enable virtual display feature. This feature provides a virtual display hardware on headless boards
 * or in virtualized environments. It will be set like xxxx:xx:xx.x,x;xxxx:xx:xx.x,x. It's the pci address of
 * the device, plus the number of crtcs to expose. E.g., 0000:26:00.0,4 would enable 4 virtual crtcs on the pci
 * device at 26:00.0. The default is NULL.
 */
MODULE_PARM_DESC(virtual_display,
		 "Enable virtual display feature (the virtual_display will be set like xxxx:xx:xx.x,x;xxxx:xx:xx.x,x)");
module_param_named(virtual_display, amdgpu_virtual_display, charp, 0444);

/**
 * DOC: lbpw (int)
 * Override Load Balancing Per Watt (LBPW) support (1 = enable, 0 = disable). The default is -1 (auto, enabled).
 */
MODULE_PARM_DESC(lbpw, "Load Balancing Per Watt (LBPW) support (1 = enable, 0 = disable, -1 = auto)");
module_param_named(lbpw, amdgpu_lbpw, int, 0444);

MODULE_PARM_DESC(compute_multipipe, "Force compute queues to be spread across pipes (1 = enable, 0 = disable, -1 = auto)");
module_param_named(compute_multipipe, amdgpu_compute_multipipe, int, 0444);

/**
 * DOC: gpu_recovery (int)
 * Set to enable GPU recovery mechanism (1 = enable, 0 = disable). The default is -1 (auto, disabled except SRIOV).
 */
MODULE_PARM_DESC(gpu_recovery, "Enable GPU recovery mechanism, (1 = enable, 0 = disable, -1 = auto)");
module_param_named_unsafe(gpu_recovery, amdgpu_gpu_recovery, int, 0444);

/**
 * DOC: emu_mode (int)
 * Set value 1 to enable emulation mode. This is only needed when running on an emulator. The default is 0 (disabled).
 */
MODULE_PARM_DESC(emu_mode, "Emulation mode, (1 = enable, 0 = disable)");
module_param_named_unsafe(emu_mode, amdgpu_emu_mode, int, 0444);

/**
 * DOC: ras_enable (int)
 * Enable RAS features on the GPU (0 = disable, 1 = enable, -1 = auto (default))
 */
MODULE_PARM_DESC(ras_enable, "Enable RAS features on the GPU (0 = disable, 1 = enable, -1 = auto (default))");
module_param_named(ras_enable, amdgpu_ras_enable, int, 0444);

/**
 * DOC: ras_mask (uint)
 * Mask of RAS features to enable (default 0xffffffff), only valid when ras_enable == 1
 * See the flags in drivers/gpu/drm/amd/amdgpu/amdgpu_ras.h
 */
MODULE_PARM_DESC(ras_mask, "Mask of RAS features to enable (default 0xffffffff), only valid when ras_enable == 1");
module_param_named(ras_mask, amdgpu_ras_mask, uint, 0444);

/**
 * DOC: timeout_fatal_disable (bool)
 * Disable Watchdog timeout fatal error event
 */
MODULE_PARM_DESC(timeout_fatal_disable, "disable watchdog timeout fatal error (false = default)");
module_param_named(timeout_fatal_disable, amdgpu_watchdog_timer.timeout_fatal_disable, bool, 0644);

/**
 * DOC: timeout_period (uint)
 * Modify the watchdog timeout max_cycles as (1 << period)
 */
MODULE_PARM_DESC(timeout_period, "watchdog timeout period (0 = timeout disabled, 1 ~ 0x23 = timeout maxcycles = (1 << period)");
module_param_named(timeout_period, amdgpu_watchdog_timer.period, uint, 0644);

/**
 * DOC: si_support (int)
 * Set SI support driver. This parameter works after set config CONFIG_DRM_AMDGPU_SI. For SI asic, when radeon driver is enabled,
 * set value 0 to use radeon driver, while set value 1 to use amdgpu driver. The default is using radeon driver when it available,
 * otherwise using amdgpu driver.
 */
#ifdef CONFIG_DRM_AMDGPU_SI

#if IS_ENABLED(CONFIG_DRM_RADEON) || IS_ENABLED(CONFIG_DRM_RADEON_MODULE)
int amdgpu_si_support;
MODULE_PARM_DESC(si_support, "SI support (1 = enabled, 0 = disabled (default))");
#else
int amdgpu_si_support = 1;
MODULE_PARM_DESC(si_support, "SI support (1 = enabled (default), 0 = disabled)");
#endif

module_param_named(si_support, amdgpu_si_support, int, 0444);
#endif

/**
 * DOC: cik_support (int)
 * Set CIK support driver. This parameter works after set config CONFIG_DRM_AMDGPU_CIK. For CIK asic, when radeon driver is enabled,
 * set value 0 to use radeon driver, while set value 1 to use amdgpu driver. The default is using radeon driver when it available,
 * otherwise using amdgpu driver.
 */
#ifdef CONFIG_DRM_AMDGPU_CIK

#if IS_ENABLED(CONFIG_DRM_RADEON) || IS_ENABLED(CONFIG_DRM_RADEON_MODULE)
int amdgpu_cik_support;
MODULE_PARM_DESC(cik_support, "CIK support (1 = enabled, 0 = disabled (default))");
#else
int amdgpu_cik_support = 1;
MODULE_PARM_DESC(cik_support, "CIK support (1 = enabled (default), 0 = disabled)");
#endif

module_param_named(cik_support, amdgpu_cik_support, int, 0444);
#endif

/**
 * DOC: smu_memory_pool_size (uint)
 * It is used to reserve gtt for smu debug usage, setting value 0 to disable it. The actual size is value * 256MiB.
 * E.g. 0x1 = 256Mbyte, 0x2 = 512Mbyte, 0x4 = 1 Gbyte, 0x8 = 2GByte. The default is 0 (disabled).
 */
MODULE_PARM_DESC(smu_memory_pool_size,
	"reserve gtt for smu debug usage, 0 = disable,0x1 = 256Mbyte, 0x2 = 512Mbyte, 0x4 = 1 Gbyte, 0x8 = 2GByte");
module_param_named(smu_memory_pool_size, amdgpu_smu_memory_pool_size, uint, 0444);

/**
 * DOC: async_gfx_ring (int)
 * It is used to enable gfx rings that could be configured with different prioritites or equal priorities
 */
MODULE_PARM_DESC(async_gfx_ring,
	"Asynchronous GFX rings that could be configured with either different priorities (HP3D ring and LP3D ring), or equal priorities (0 = disabled, 1 = enabled (default))");
module_param_named(async_gfx_ring, amdgpu_async_gfx_ring, int, 0444);

/**
 * DOC: mcbp (int)
 * It is used to enable mid command buffer preemption. (0 = disabled, 1 = enabled, -1 auto (default))
 */
MODULE_PARM_DESC(mcbp,
	"Enable Mid-command buffer preemption (0 = disabled, 1 = enabled), -1 = auto (default)");
module_param_named(mcbp, amdgpu_mcbp, int, 0444);

/**
 * DOC: discovery (int)
 * Allow driver to discover hardware IP information from IP Discovery table at the top of VRAM.
 * (-1 = auto (default), 0 = disabled, 1 = enabled, 2 = use ip_discovery table from file)
 */
MODULE_PARM_DESC(discovery,
	"Allow driver to discover hardware IPs from IP Discovery table at the top of VRAM");
module_param_named(discovery, amdgpu_discovery, int, 0444);

/**
 * DOC: mes (int)
 * Enable Micro Engine Scheduler. This is a new hw scheduling engine for gfx, sdma, and compute.
 * (0 = disabled (default), 1 = enabled)
 */
MODULE_PARM_DESC(mes,
	"Enable Micro Engine Scheduler (0 = disabled (default), 1 = enabled)");
module_param_named(mes, amdgpu_mes, int, 0444);

/**
 * DOC: mes_log_enable (int)
 * Enable Micro Engine Scheduler log. This is used to enable/disable MES internal log.
 * (0 = disabled (default), 1 = enabled)
 */
MODULE_PARM_DESC(mes_log_enable,
	"Enable Micro Engine Scheduler log (0 = disabled (default), 1 = enabled)");
module_param_named(mes_log_enable, amdgpu_mes_log_enable, int, 0444);

/**
 * DOC: mes_kiq (int)
 * Enable Micro Engine Scheduler KIQ. This is a new engine pipe for kiq.
 * (0 = disabled (default), 1 = enabled)
 */
MODULE_PARM_DESC(mes_kiq,
	"Enable Micro Engine Scheduler KIQ (0 = disabled (default), 1 = enabled)");
module_param_named(mes_kiq, amdgpu_mes_kiq, int, 0444);

/**
 * DOC: uni_mes (int)
 * Enable Unified Micro Engine Scheduler. This is a new engine pipe for unified scheduler.
 * (0 = disabled (default), 1 = enabled)
 */
MODULE_PARM_DESC(uni_mes,
	"Enable Unified Micro Engine Scheduler (0 = disabled, 1 = enabled(default)");
module_param_named(uni_mes, amdgpu_uni_mes, int, 0444);

/**
 * DOC: noretry (int)
 * Disable XNACK retry in the SQ by default on GFXv9 hardware. On ASICs that
 * do not support per-process XNACK this also disables retry page faults.
 * (0 = retry enabled, 1 = retry disabled, -1 auto (default))
 */
MODULE_PARM_DESC(noretry,
	"Disable retry faults (0 = retry enabled, 1 = retry disabled, -1 auto (default))");
module_param_named(noretry, amdgpu_noretry, int, 0644);

/**
 * DOC: force_asic_type (int)
 * A non negative value used to specify the asic type for all supported GPUs.
 */
MODULE_PARM_DESC(force_asic_type,
	"A non negative value used to specify the asic type for all supported GPUs");
module_param_named_unsafe(force_asic_type, amdgpu_force_asic_type, int, 0444);

/**
 * DOC: use_xgmi_p2p (int)
 * Enables/disables XGMI P2P interface (0 = disable, 1 = enable).
 */
MODULE_PARM_DESC(use_xgmi_p2p,
	"Enable XGMI P2P interface (0 = disable; 1 = enable (default))");
module_param_named(use_xgmi_p2p, amdgpu_use_xgmi_p2p, int, 0444);


#ifdef CONFIG_HSA_AMD
/**
 * DOC: sched_policy (int)
 * Set scheduling policy. Default is HWS(hardware scheduling) with over-subscription.
 * Setting 1 disables over-subscription. Setting 2 disables HWS and statically
 * assigns queues to HQDs.
 */
int sched_policy = KFD_SCHED_POLICY_HWS;
module_param_unsafe(sched_policy, int, 0444);
MODULE_PARM_DESC(sched_policy,
	"Scheduling policy (0 = HWS (Default), 1 = HWS without over-subscription, 2 = Non-HWS (Used for debugging only)");

/**
 * DOC: hws_max_conc_proc (int)
 * Maximum number of processes that HWS can schedule concurrently. The maximum is the
 * number of VMIDs assigned to the HWS, which is also the default.
 */
int hws_max_conc_proc = -1;
module_param(hws_max_conc_proc, int, 0444);
MODULE_PARM_DESC(hws_max_conc_proc,
	"Max # processes HWS can execute concurrently when sched_policy=0 (0 = no concurrency, #VMIDs for KFD = Maximum(default))");

/**
 * DOC: cwsr_enable (int)
 * CWSR(compute wave store and resume) allows the GPU to preempt shader execution in
 * the middle of a compute wave. Default is 1 to enable this feature. Setting 0
 * disables it.
 */
int cwsr_enable = 1;
module_param(cwsr_enable, int, 0444);
MODULE_PARM_DESC(cwsr_enable, "CWSR enable (0 = Off, 1 = On (Default))");

/**
 * DOC: max_num_of_queues_per_device (int)
 * Maximum number of queues per device. Valid setting is between 1 and 4096. Default
 * is 4096.
 */
int max_num_of_queues_per_device = KFD_MAX_NUM_OF_QUEUES_PER_DEVICE_DEFAULT;
module_param(max_num_of_queues_per_device, int, 0444);
MODULE_PARM_DESC(max_num_of_queues_per_device,
	"Maximum number of supported queues per device (1 = Minimum, 4096 = default)");

/**
 * DOC: send_sigterm (int)
 * Send sigterm to HSA process on unhandled exceptions. Default is not to send sigterm
 * but just print errors on dmesg. Setting 1 enables sending sigterm.
 */
int send_sigterm;
module_param(send_sigterm, int, 0444);
MODULE_PARM_DESC(send_sigterm,
	"Send sigterm to HSA process on unhandled exception (0 = disable, 1 = enable)");

/**
 * DOC: halt_if_hws_hang (int)
 * Halt if HWS hang is detected. Default value, 0, disables the halt on hang.
 * Setting 1 enables halt on hang.
 */
int halt_if_hws_hang;
module_param_unsafe(halt_if_hws_hang, int, 0644);
MODULE_PARM_DESC(halt_if_hws_hang, "Halt if HWS hang is detected (0 = off (default), 1 = on)");

/**
 * DOC: hws_gws_support(bool)
 * Assume that HWS supports GWS barriers regardless of what firmware version
 * check says. Default value: false (rely on MEC2 firmware version check).
 */
bool hws_gws_support;
module_param_unsafe(hws_gws_support, bool, 0444);
MODULE_PARM_DESC(hws_gws_support, "Assume MEC2 FW supports GWS barriers (false = rely on FW version check (Default), true = force supported)");

/**
 * DOC: queue_preemption_timeout_ms (int)
 * queue preemption timeout in ms (1 = Minimum, 9000 = default)
 */
int queue_preemption_timeout_ms = 9000;
module_param(queue_preemption_timeout_ms, int, 0644);
MODULE_PARM_DESC(queue_preemption_timeout_ms, "queue preemption timeout in ms (1 = Minimum, 9000 = default)");

/**
 * DOC: debug_evictions(bool)
 * Enable extra debug messages to help determine the cause of evictions
 */
bool debug_evictions;
module_param(debug_evictions, bool, 0644);
MODULE_PARM_DESC(debug_evictions, "enable eviction debug messages (false = default)");

/**
 * DOC: no_system_mem_limit(bool)
 * Disable system memory limit, to support multiple process shared memory
 */
bool no_system_mem_limit;
module_param(no_system_mem_limit, bool, 0644);
MODULE_PARM_DESC(no_system_mem_limit, "disable system memory limit (false = default)");

/**
 * DOC: no_queue_eviction_on_vm_fault (int)
 * If set, process queues will not be evicted on gpuvm fault. This is to keep the wavefront context for debugging (0 = queue eviction, 1 = no queue eviction). The default is 0 (queue eviction).
 */
int amdgpu_no_queue_eviction_on_vm_fault;
MODULE_PARM_DESC(no_queue_eviction_on_vm_fault, "No queue eviction on VM fault (0 = queue eviction, 1 = no queue eviction)");
module_param_named_unsafe(no_queue_eviction_on_vm_fault, amdgpu_no_queue_eviction_on_vm_fault, int, 0444);
#endif

/**
 * DOC: mtype_local (int)
 */
int amdgpu_mtype_local;
MODULE_PARM_DESC(mtype_local, "MTYPE for local memory (0 = MTYPE_RW (default), 1 = MTYPE_NC, 2 = MTYPE_CC)");
module_param_named_unsafe(mtype_local, amdgpu_mtype_local, int, 0444);

/**
 * DOC: pcie_p2p (bool)
 * Enable PCIe P2P (requires large-BAR). Default value: true (on)
 */
#ifdef CONFIG_HSA_AMD_P2P
bool pcie_p2p = true;
module_param(pcie_p2p, bool, 0444);
MODULE_PARM_DESC(pcie_p2p, "Enable PCIe P2P (requires large-BAR). (N = off, Y = on(default))");
#endif

/**
 * DOC: dcfeaturemask (uint)
 * Override display features enabled. See enum DC_FEATURE_MASK in drivers/gpu/drm/amd/include/amd_shared.h.
 * The default is the current set of stable display features.
 */
MODULE_PARM_DESC(dcfeaturemask, "all stable DC features enabled (default))");
module_param_named(dcfeaturemask, amdgpu_dc_feature_mask, uint, 0444);

/**
 * DOC: dcdebugmask (uint)
 * Override display features enabled. See enum DC_DEBUG_MASK in drivers/gpu/drm/amd/include/amd_shared.h.
 */
MODULE_PARM_DESC(dcdebugmask, "all debug options disabled (default))");
module_param_named(dcdebugmask, amdgpu_dc_debug_mask, uint, 0444);

MODULE_PARM_DESC(visualconfirm, "Visual confirm (0 = off (default), 1 = MPO, 5 = PSR)");
module_param_named(visualconfirm, amdgpu_dc_visual_confirm, uint, 0444);

/**
 * DOC: abmlevel (uint)
 * Override the default ABM (Adaptive Backlight Management) level used for DC
 * enabled hardware. Requires DMCU to be supported and loaded.
 * Valid levels are 0-4. A value of 0 indicates that ABM should be disabled by
 * default. Values 1-4 control the maximum allowable brightness reduction via
 * the ABM algorithm, with 1 being the least reduction and 4 being the most
 * reduction.
 *
 * Defaults to -1, or auto. Userspace can only override this level after
 * boot if it's set to auto.
 */
int amdgpu_dm_abm_level = -1;
MODULE_PARM_DESC(abmlevel,
		 "ABM level (0 = off, 1-4 = backlight reduction level, -1 auto (default))");
module_param_named(abmlevel, amdgpu_dm_abm_level, int, 0444);

int amdgpu_backlight = -1;
MODULE_PARM_DESC(backlight, "Backlight control (0 = pwm, 1 = aux, -1 auto (default))");
module_param_named(backlight, amdgpu_backlight, bint, 0444);

/**
 * DOC: damageclips (int)
 * Enable or disable damage clips support. If damage clips support is disabled,
 * we will force full frame updates, irrespective of what user space sends to
 * us.
 *
 * Defaults to -1 (where it is enabled unless a PSR-SU display is detected).
 */
MODULE_PARM_DESC(damageclips,
		 "Damage clips support (0 = disable, 1 = enable, -1 auto (default))");
module_param_named(damageclips, amdgpu_damage_clips, int, 0444);

/**
 * DOC: tmz (int)
 * Trusted Memory Zone (TMZ) is a method to protect data being written
 * to or read from memory.
 *
 * The default value: 0 (off).  TODO: change to auto till it is completed.
 */
MODULE_PARM_DESC(tmz, "Enable TMZ feature (-1 = auto (default), 0 = off, 1 = on)");
module_param_named(tmz, amdgpu_tmz, int, 0444);

/**
 * DOC: freesync_video (uint)
 * Enable the optimization to adjust front porch timing to achieve seamless
 * mode change experience when setting a freesync supported mode for which full
 * modeset is not needed.
 *
 * The Display Core will add a set of modes derived from the base FreeSync
 * video mode into the corresponding connector's mode list based on commonly
 * used refresh rates and VRR range of the connected display, when users enable
 * this feature. From the userspace perspective, they can see a seamless mode
 * change experience when the change between different refresh rates under the
 * same resolution. Additionally, userspace applications such as Video playback
 * can read this modeset list and change the refresh rate based on the video
 * frame rate. Finally, the userspace can also derive an appropriate mode for a
 * particular refresh rate based on the FreeSync Mode and add it to the
 * connector's mode list.
 *
 * Note: This is an experimental feature.
 *
 * The default value: 0 (off).
 */
MODULE_PARM_DESC(
	freesync_video,
	"Enable freesync modesetting optimization feature (0 = off (default), 1 = on)");
module_param_named(freesync_video, amdgpu_freesync_vid_mode, uint, 0444);

/**
 * DOC: reset_method (int)
 * GPU reset method (-1 = auto (default), 0 = legacy, 1 = mode0, 2 = mode1, 3 = mode2, 4 = baco)
 */
MODULE_PARM_DESC(reset_method, "GPU reset method (-1 = auto (default), 0 = legacy, 1 = mode0, 2 = mode1, 3 = mode2, 4 = baco/bamaco)");
module_param_named_unsafe(reset_method, amdgpu_reset_method, int, 0644);

/**
 * DOC: bad_page_threshold (int) Bad page threshold is specifies the
 * threshold value of faulty pages detected by RAS ECC, which may
 * result in the GPU entering bad status when the number of total
 * faulty pages by ECC exceeds the threshold value.
 */
MODULE_PARM_DESC(bad_page_threshold, "Bad page threshold(-1 = ignore threshold (default value), 0 = disable bad page retirement, -2 = threshold determined by a formula, 0 < threshold < max records, user-defined threshold)");
module_param_named(bad_page_threshold, amdgpu_bad_page_threshold, int, 0444);

MODULE_PARM_DESC(num_kcq, "number of kernel compute queue user want to setup (8 if set to greater than 8 or less than 0, only affect gfx 8+)");
module_param_named(num_kcq, amdgpu_num_kcq, int, 0444);

/**
 * DOC: vcnfw_log (int)
 * Enable vcnfw log output for debugging, the default is disabled.
 */
MODULE_PARM_DESC(vcnfw_log, "Enable vcnfw log(0 = disable (default value), 1 = enable)");
module_param_named(vcnfw_log, amdgpu_vcnfw_log, int, 0444);

/**
 * DOC: sg_display (int)
 * Disable S/G (scatter/gather) display (i.e., display from system memory).
 * This option is only relevant on APUs.  Set this option to 0 to disable
 * S/G display if you experience flickering or other issues under memory
 * pressure and report the issue.
 */
MODULE_PARM_DESC(sg_display, "S/G Display (-1 = auto (default), 0 = disable)");
module_param_named(sg_display, amdgpu_sg_display, int, 0444);

/**
 * DOC: umsch_mm (int)
 * Enable Multi Media User Mode Scheduler. This is a HW scheduling engine for VCN and VPE.
 * (0 = disabled (default), 1 = enabled)
 */
MODULE_PARM_DESC(umsch_mm,
	"Enable Multi Media User Mode Scheduler (0 = disabled (default), 1 = enabled)");
module_param_named(umsch_mm, amdgpu_umsch_mm, int, 0444);

/**
 * DOC: umsch_mm_fwlog (int)
 * Enable umschfw log output for debugging, the default is disabled.
 */
MODULE_PARM_DESC(umsch_mm_fwlog, "Enable umschfw log(0 = disable (default value), 1 = enable)");
module_param_named(umsch_mm_fwlog, amdgpu_umsch_mm_fwlog, int, 0444);

/**
 * DOC: smu_pptable_id (int)
 * Used to override pptable id. id = 0 use VBIOS pptable.
 * id > 0 use the soft pptable with specicfied id.
 */
MODULE_PARM_DESC(smu_pptable_id,
	"specify pptable id to be used (-1 = auto(default) value, 0 = use pptable from vbios, > 0 = soft pptable id)");
module_param_named(smu_pptable_id, amdgpu_smu_pptable_id, int, 0444);

/**
 * DOC: partition_mode (int)
 * Used to override the default SPX mode.
 */
MODULE_PARM_DESC(
	user_partt_mode,
	"specify partition mode to be used (-2 = AMDGPU_AUTO_COMPUTE_PARTITION_MODE(default value) \
						0 = AMDGPU_SPX_PARTITION_MODE, \
						1 = AMDGPU_DPX_PARTITION_MODE, \
						2 = AMDGPU_TPX_PARTITION_MODE, \
						3 = AMDGPU_QPX_PARTITION_MODE, \
						4 = AMDGPU_CPX_PARTITION_MODE)");
module_param_named(user_partt_mode, amdgpu_user_partt_mode, uint, 0444);


/**
 * DOC: enforce_isolation (int)
 * enforce process isolation between graphics and compute.
 * (-1 = auto, 0 = disable, 1 = enable, 2 = enable legacy mode, 3 = enable without cleaner shader)
 */
module_param_named(enforce_isolation, amdgpu_enforce_isolation, int, 0444);
MODULE_PARM_DESC(enforce_isolation,
"enforce process isolation between graphics and compute. (-1 = auto, 0 = disable, 1 = enable, 2 = enable legacy mode, 3 = enable without cleaner shader)");

/**
 * DOC: modeset (int)
 * Override nomodeset (1 = override, -1 = auto). The default is -1 (auto).
 */
MODULE_PARM_DESC(modeset, "Override nomodeset (1 = enable, -1 = auto)");
module_param_named(modeset, amdgpu_modeset, int, 0444);

/**
 * DOC: seamless (int)
 * Seamless boot will keep the image on the screen during the boot process.
 */
MODULE_PARM_DESC(seamless, "Seamless boot (-1 = auto (default), 0 = disable, 1 = enable)");
module_param_named(seamless, amdgpu_seamless, int, 0444);

/**
 * DOC: debug_mask (uint)
 * Debug options for amdgpu, work as a binary mask with the following options:
 *
 * - 0x1: Debug VM handling
 * - 0x2: Enable simulating large-bar capability on non-large bar system. This
 *   limits the VRAM size reported to ROCm applications to the visible
 *   size, usually 256MB.
 * - 0x4: Disable GPU soft recovery, always do a full reset
 * - 0x8: Use VRAM for firmware loading
 * - 0x10: Enable ACA based RAS logging
 * - 0x20: Enable experimental resets
 * - 0x40: Disable ring resets
 * - 0x80: Use VRAM for SMU pool
 */
MODULE_PARM_DESC(debug_mask, "debug options for amdgpu, disabled by default");
module_param_named_unsafe(debug_mask, amdgpu_debug_mask, uint, 0444);

/**
 * DOC: agp (int)
 * Enable the AGP aperture.  This provides an aperture in the GPU's internal
 * address space for direct access to system memory.  Note that these accesses
 * are non-snooped, so they are only used for access to uncached memory.
 */
MODULE_PARM_DESC(agp, "AGP (-1 = auto (default), 0 = disable, 1 = enable)");
module_param_named(agp, amdgpu_agp, int, 0444);

/**
 * DOC: wbrf (int)
 * Enable Wifi RFI interference mitigation feature.
 * Due to electrical and mechanical constraints there may be likely interference of
 * relatively high-powered harmonics of the (G-)DDR memory clocks with local radio
 * module frequency bands used by Wifi 6/6e/7. To mitigate the possible RFI interference,
 * with this feature enabled, PMFW will use either “shadowed P-State” or “P-State” based
 * on active list of frequencies in-use (to be avoided) as part of initial setting or
 * P-state transition. However, there may be potential performance impact with this
 * feature enabled.
 * (0 = disabled, 1 = enabled, -1 = auto (default setting, will be enabled if supported))
 */
MODULE_PARM_DESC(wbrf,
	"Enable Wifi RFI interference mitigation (0 = disabled, 1 = enabled, -1 = auto(default)");
module_param_named(wbrf, amdgpu_wbrf, int, 0444);

/**
 * DOC: rebar (int)
 * Allow BAR resizing.  Disable this to prevent the driver from attempting
 * to resize the BAR if the GPU supports it and there is available MMIO space.
 * Note that this just prevents the driver from resizing the BAR.  The BIOS
 * may have already resized the BAR at boot time.
 */
MODULE_PARM_DESC(rebar, "Resizable BAR (-1 = auto (default), 0 = disable, 1 = enable)");
module_param_named(rebar, amdgpu_rebar, int, 0444);

/**
 * DOC: user_queue (int)
 * Enable user queues on systems that support user queues. Possible values:
 *
 * - -1 = auto (ASIC specific default)
 * -  0 = user queues disabled
 * -  1 = user queues enabled and kernel queues enabled (if supported)
 * -  2 = user queues enabled and kernel queues disabled
 */
MODULE_PARM_DESC(user_queue, "Enable user queues (-1 = auto (default), 0 = disable, 1 = enable, 2 = enable UQs and disable KQs)");
module_param_named(user_queue, amdgpu_user_queue, int, 0444);

/* These devices are not supported by amdgpu.
 * They are supported by the mach64, r128, radeon drivers
 */
static const u16 amdgpu_unsupported_pciidlist[] = {
	/* mach64 */
	0x4354,
	0x4358,
	0x4554,
	0x4742,
	0x4744,
	0x4749,
	0x474C,
	0x474D,
	0x474E,
	0x474F,
	0x4750,
	0x4751,
	0x4752,
	0x4753,
	0x4754,
	0x4755,
	0x4756,
	0x4757,
	0x4758,
	0x4759,
	0x475A,
	0x4C42,
	0x4C44,
	0x4C47,
	0x4C49,
	0x4C4D,
	0x4C4E,
	0x4C50,
	0x4C51,
	0x4C52,
	0x4C53,
	0x5654,
	0x5655,
	0x5656,
	/* r128 */
	0x4c45,
	0x4c46,
	0x4d46,
	0x4d4c,
	0x5041,
	0x5042,
	0x5043,
	0x5044,
	0x5045,
	0x5046,
	0x5047,
	0x5048,
	0x5049,
	0x504A,
	0x504B,
	0x504C,
	0x504D,
	0x504E,
	0x504F,
	0x5050,
	0x5051,
	0x5052,
	0x5053,
	0x5054,
	0x5055,
	0x5056,
	0x5057,
	0x5058,
	0x5245,
	0x5246,
	0x5247,
	0x524b,
	0x524c,
	0x534d,
	0x5446,
	0x544C,
	0x5452,
	/* radeon */
	0x3150,
	0x3151,
	0x3152,
	0x3154,
	0x3155,
	0x3E50,
	0x3E54,
	0x4136,
	0x4137,
	0x4144,
	0x4145,
	0x4146,
	0x4147,
	0x4148,
	0x4149,
	0x414A,
	0x414B,
	0x4150,
	0x4151,
	0x4152,
	0x4153,
	0x4154,
	0x4155,
	0x4156,
	0x4237,
	0x4242,
	0x4336,
	0x4337,
	0x4437,
	0x4966,
	0x4967,
	0x4A48,
	0x4A49,
	0x4A4A,
	0x4A4B,
	0x4A4C,
	0x4A4D,
	0x4A4E,
	0x4A4F,
	0x4A50,
	0x4A54,
	0x4B48,
	0x4B49,
	0x4B4A,
	0x4B4B,
	0x4B4C,
	0x4C57,
	0x4C58,
	0x4C59,
	0x4C5A,
	0x4C64,
	0x4C66,
	0x4C67,
	0x4E44,
	0x4E45,
	0x4E46,
	0x4E47,
	0x4E48,
	0x4E49,
	0x4E4A,
	0x4E4B,
	0x4E50,
	0x4E51,
	0x4E52,
	0x4E53,
	0x4E54,
	0x4E56,
	0x5144,
	0x5145,
	0x5146,
	0x5147,
	0x5148,
	0x514C,
	0x514D,
	0x5157,
	0x5158,
	0x5159,
	0x515A,
	0x515E,
	0x5460,
	0x5462,
	0x5464,
	0x5548,
	0x5549,
	0x554A,
	0x554B,
	0x554C,
	0x554D,
	0x554E,
	0x554F,
	0x5550,
	0x5551,
	0x5552,
	0x5554,
	0x564A,
	0x564B,
	0x564F,
	0x5652,
	0x5653,
	0x5657,
	0x5834,
	0x5835,
	0x5954,
	0x5955,
	0x5974,
	0x5975,
	0x5960,
	0x5961,
	0x5962,
	0x5964,
	0x5965,
	0x5969,
	0x5a41,
	0x5a42,
	0x5a61,
	0x5a62,
	0x5b60,
	0x5b62,
	0x5b63,
	0x5b64,
	0x5b65,
	0x5c61,
	0x5c63,
	0x5d48,
	0x5d49,
	0x5d4a,
	0x5d4c,
	0x5d4d,
	0x5d4e,
	0x5d4f,
	0x5d50,
	0x5d52,
	0x5d57,
	0x5e48,
	0x5e4a,
	0x5e4b,
	0x5e4c,
	0x5e4d,
	0x5e4f,
	0x6700,
	0x6701,
	0x6702,
	0x6703,
	0x6704,
	0x6705,
	0x6706,
	0x6707,
	0x6708,
	0x6709,
	0x6718,
	0x6719,
	0x671c,
	0x671d,
	0x671f,
	0x6720,
	0x6721,
	0x6722,
	0x6723,
	0x6724,
	0x6725,
	0x6726,
	0x6727,
	0x6728,
	0x6729,
	0x6738,
	0x6739,
	0x673e,
	0x6740,
	0x6741,
	0x6742,
	0x6743,
	0x6744,
	0x6745,
	0x6746,
	0x6747,
	0x6748,
	0x6749,
	0x674A,
	0x6750,
	0x6751,
	0x6758,
	0x6759,
	0x675B,
	0x675D,
	0x675F,
	0x6760,
	0x6761,
	0x6762,
	0x6763,
	0x6764,
	0x6765,
	0x6766,
	0x6767,
	0x6768,
	0x6770,
	0x6771,
	0x6772,
	0x6778,
	0x6779,
	0x677B,
	0x6840,
	0x6841,
	0x6842,
	0x6843,
	0x6849,
	0x684C,
	0x6850,
	0x6858,
	0x6859,
	0x6880,
	0x6888,
	0x6889,
	0x688A,
	0x688C,
	0x688D,
	0x6898,
	0x6899,
	0x689b,
	0x689c,
	0x689d,
	0x689e,
	0x68a0,
	0x68a1,
	0x68a8,
	0x68a9,
	0x68b0,
	0x68b8,
	0x68b9,
	0x68ba,
	0x68be,
	0x68bf,
	0x68c0,
	0x68c1,
	0x68c7,
	0x68c8,
	0x68c9,
	0x68d8,
	0x68d9,
	0x68da,
	0x68de,
	0x68e0,
	0x68e1,
	0x68e4,
	0x68e5,
	0x68e8,
	0x68e9,
	0x68f1,
	0x68f2,
	0x68f8,
	0x68f9,
	0x68fa,
	0x68fe,
	0x7100,
	0x7101,
	0x7102,
	0x7103,
	0x7104,
	0x7105,
	0x7106,
	0x7108,
	0x7109,
	0x710A,
	0x710B,
	0x710C,
	0x710E,
	0x710F,
	0x7140,
	0x7141,
	0x7142,
	0x7143,
	0x7144,
	0x7145,
	0x7146,
	0x7147,
	0x7149,
	0x714A,
	0x714B,
	0x714C,
	0x714D,
	0x714E,
	0x714F,
	0x7151,
	0x7152,
	0x7153,
	0x715E,
	0x715F,
	0x7180,
	0x7181,
	0x7183,
	0x7186,
	0x7187,
	0x7188,
	0x718A,
	0x718B,
	0x718C,
	0x718D,
	0x718F,
	0x7193,
	0x7196,
	0x719B,
	0x719F,
	0x71C0,
	0x71C1,
	0x71C2,
	0x71C3,
	0x71C4,
	0x71C5,
	0x71C6,
	0x71C7,
	0x71CD,
	0x71CE,
	0x71D2,
	0x71D4,
	0x71D5,
	0x71D6,
	0x71DA,
	0x71DE,
	0x7200,
	0x7210,
	0x7211,
	0x7240,
	0x7243,
	0x7244,
	0x7245,
	0x7246,
	0x7247,
	0x7248,
	0x7249,
	0x724A,
	0x724B,
	0x724C,
	0x724D,
	0x724E,
	0x724F,
	0x7280,
	0x7281,
	0x7283,
	0x7284,
	0x7287,
	0x7288,
	0x7289,
	0x728B,
	0x728C,
	0x7290,
	0x7291,
	0x7293,
	0x7297,
	0x7834,
	0x7835,
	0x791e,
	0x791f,
	0x793f,
	0x7941,
	0x7942,
	0x796c,
	0x796d,
	0x796e,
	0x796f,
	0x9400,
	0x9401,
	0x9402,
	0x9403,
	0x9405,
	0x940A,
	0x940B,
	0x940F,
	0x94A0,
	0x94A1,
	0x94A3,
	0x94B1,
	0x94B3,
	0x94B4,
	0x94B5,
	0x94B9,
	0x9440,
	0x9441,
	0x9442,
	0x9443,
	0x9444,
	0x9446,
	0x944A,
	0x944B,
	0x944C,
	0x944E,
	0x9450,
	0x9452,
	0x9456,
	0x945A,
	0x945B,
	0x945E,
	0x9460,
	0x9462,
	0x946A,
	0x946B,
	0x947A,
	0x947B,
	0x9480,
	0x9487,
	0x9488,
	0x9489,
	0x948A,
	0x948F,
	0x9490,
	0x9491,
	0x9495,
	0x9498,
	0x949C,
	0x949E,
	0x949F,
	0x94C0,
	0x94C1,
	0x94C3,
	0x94C4,
	0x94C5,
	0x94C6,
	0x94C7,
	0x94C8,
	0x94C9,
	0x94CB,
	0x94CC,
	0x94CD,
	0x9500,
	0x9501,
	0x9504,
	0x9505,
	0x9506,
	0x9507,
	0x9508,
	0x9509,
	0x950F,
	0x9511,
	0x9515,
	0x9517,
	0x9519,
	0x9540,
	0x9541,
	0x9542,
	0x954E,
	0x954F,
	0x9552,
	0x9553,
	0x9555,
	0x9557,
	0x955f,
	0x9580,
	0x9581,
	0x9583,
	0x9586,
	0x9587,
	0x9588,
	0x9589,
	0x958A,
	0x958B,
	0x958C,
	0x958D,
	0x958E,
	0x958F,
	0x9590,
	0x9591,
	0x9593,
	0x9595,
	0x9596,
	0x9597,
	0x9598,
	0x9599,
	0x959B,
	0x95C0,
	0x95C2,
	0x95C4,
	0x95C5,
	0x95C6,
	0x95C7,
	0x95C9,
	0x95CC,
	0x95CD,
	0x95CE,
	0x95CF,
	0x9610,
	0x9611,
	0x9612,
	0x9613,
	0x9614,
	0x9615,
	0x9616,
	0x9640,
	0x9641,
	0x9642,
	0x9643,
	0x9644,
	0x9645,
	0x9647,
	0x9648,
	0x9649,
	0x964a,
	0x964b,
	0x964c,
	0x964e,
	0x964f,
	0x9710,
	0x9711,
	0x9712,
	0x9713,
	0x9714,
	0x9715,
	0x9802,
	0x9803,
	0x9804,
	0x9805,
	0x9806,
	0x9807,
	0x9808,
	0x9809,
	0x980A,
	0x9900,
	0x9901,
	0x9903,
	0x9904,
	0x9905,
	0x9906,
	0x9907,
	0x9908,
	0x9909,
	0x990A,
	0x990B,
	0x990C,
	0x990D,
	0x990E,
	0x990F,
	0x9910,
	0x9913,
	0x9917,
	0x9918,
	0x9919,
	0x9990,
	0x9991,
	0x9992,
	0x9993,
	0x9994,
	0x9995,
	0x9996,
	0x9997,
	0x9998,
	0x9999,
	0x999A,
	0x999B,
	0x999C,
	0x999D,
	0x99A0,
	0x99A2,
	0x99A4,
	/* radeon secondary ids */
	0x3171,
	0x3e70,
	0x4164,
	0x4165,
	0x4166,
	0x4168,
	0x4170,
	0x4171,
	0x4172,
	0x4173,
	0x496e,
	0x4a69,
	0x4a6a,
	0x4a6b,
	0x4a70,
	0x4a74,
	0x4b69,
	0x4b6b,
	0x4b6c,
	0x4c6e,
	0x4e64,
	0x4e65,
	0x4e66,
	0x4e67,
	0x4e68,
	0x4e69,
	0x4e6a,
	0x4e71,
	0x4f73,
	0x5569,
	0x556b,
	0x556d,
	0x556f,
	0x5571,
	0x5854,
	0x5874,
	0x5940,
	0x5941,
	0x5b70,
	0x5b72,
	0x5b73,
	0x5b74,
	0x5b75,
	0x5d44,
	0x5d45,
	0x5d6d,
	0x5d6f,
	0x5d72,
	0x5d77,
	0x5e6b,
	0x5e6d,
	0x7120,
	0x7124,
	0x7129,
	0x712e,
	0x712f,
	0x7162,
	0x7163,
	0x7166,
	0x7167,
	0x7172,
	0x7173,
	0x71a0,
	0x71a1,
	0x71a3,
	0x71a7,
	0x71bb,
	0x71e0,
	0x71e1,
	0x71e2,
	0x71e6,
	0x71e7,
	0x71f2,
	0x7269,
	0x726b,
	0x726e,
	0x72a0,
	0x72a8,
	0x72b1,
	0x72b3,
	0x793f,
};

static const struct pci_device_id pciidlist[] = {
	{0x1002, 0x6780, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6784, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6788, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x678A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6790, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6791, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6792, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6798, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6799, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x679A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x679B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x679E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x679F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TAHITI},
	{0x1002, 0x6800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN|AMD_IS_MOBILITY},
	{0x1002, 0x6801, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN|AMD_IS_MOBILITY},
	{0x1002, 0x6802, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN|AMD_IS_MOBILITY},
	{0x1002, 0x6806, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6808, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6809, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6810, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6811, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6816, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6817, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6818, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6819, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_PITCAIRN},
	{0x1002, 0x6600, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6601, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6602, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6603, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6604, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6605, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6606, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6607, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6608, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND},
	{0x1002, 0x6610, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND},
	{0x1002, 0x6611, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND},
	{0x1002, 0x6613, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND},
	{0x1002, 0x6617, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6620, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6621, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6623, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND|AMD_IS_MOBILITY},
	{0x1002, 0x6631, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_OLAND},
	{0x1002, 0x6820, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6821, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6822, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6823, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6824, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6825, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6826, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6827, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6828, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x6829, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x682A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x682B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x682C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x682D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x682F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6830, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6831, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE|AMD_IS_MOBILITY},
	{0x1002, 0x6835, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x6837, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x6838, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x6839, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x683B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x683D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x683F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VERDE},
	{0x1002, 0x6660, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	{0x1002, 0x6663, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	{0x1002, 0x6664, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	{0x1002, 0x6665, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	{0x1002, 0x6667, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	{0x1002, 0x666F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAINAN|AMD_IS_MOBILITY},
	/* Kaveri */
	{0x1002, 0x1304, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1305, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1306, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1307, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1309, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x130F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1310, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1311, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1312, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1313, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1315, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1316, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x1317, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x1318, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x131B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x131C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	{0x1002, 0x131D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KAVERI|AMD_IS_APU},
	/* Bonaire */
	{0x1002, 0x6640, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6641, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6646, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6647, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE|AMD_IS_MOBILITY},
	{0x1002, 0x6649, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6650, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6651, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x6658, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	{0x1002, 0x665f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BONAIRE},
	/* Hawaii */
	{0x1002, 0x67A0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67A9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67AA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67B9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67BA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	{0x1002, 0x67BE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_HAWAII},
	/* Kabini */
	{0x1002, 0x9830, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9831, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9832, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9833, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9834, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9835, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9836, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9837, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x9838, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9839, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x983a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x983c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	{0x1002, 0x983f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_KABINI|AMD_IS_APU},
	/* mullins */
	{0x1002, 0x9850, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9851, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9852, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9853, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9854, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9855, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9856, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9857, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9858, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x9859, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	{0x1002, 0x985F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_MULLINS|AMD_IS_MOBILITY|AMD_IS_APU},
	/* topaz */
	{0x1002, 0x6900, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6901, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6902, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6903, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	{0x1002, 0x6907, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TOPAZ},
	/* tonga */
	{0x1002, 0x6920, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6921, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6928, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6929, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x692B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x692F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6930, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6938, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	{0x1002, 0x6939, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_TONGA},
	/* fiji */
	{0x1002, 0x7300, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_FIJI},
	{0x1002, 0x730F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_FIJI},
	/* carrizo */
	{0x1002, 0x9870, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9874, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9875, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9876, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	{0x1002, 0x9877, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CARRIZO|AMD_IS_APU},
	/* stoney */
	{0x1002, 0x98E4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_STONEY|AMD_IS_APU},
	/* Polaris11 */
	{0x1002, 0x67E0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67E3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67E8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67EB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67EF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67FF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67E1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67E7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	{0x1002, 0x67E9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS11},
	/* Polaris10 */
	{0x1002, 0x67C0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67D0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67DF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67C9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67CA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67CC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x67CF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	{0x1002, 0x6FDF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS10},
	/* Polaris12 */
	{0x1002, 0x6980, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6981, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6985, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6986, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6987, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6995, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x6997, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	{0x1002, 0x699F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_POLARIS12},
	/* VEGAM */
	{0x1002, 0x694C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGAM},
	{0x1002, 0x694E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGAM},
	{0x1002, 0x694F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGAM},
	/* Vega 10 */
	{0x1002, 0x6860, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6861, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6862, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6863, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6864, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6867, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6868, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x6869, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686a, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686b, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686c, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686d, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686e, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x686f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	{0x1002, 0x687f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA10},
	/* Vega 12 */
	{0x1002, 0x69A0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA12},
	{0x1002, 0x69A1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA12},
	{0x1002, 0x69A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA12},
	{0x1002, 0x69A3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA12},
	{0x1002, 0x69AF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA12},
	/* Vega 20 */
	{0x1002, 0x66A0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66A1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66A3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66A4, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66A7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	{0x1002, 0x66AF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_VEGA20},
	/* Raven */
	{0x1002, 0x15dd, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RAVEN|AMD_IS_APU},
	{0x1002, 0x15d8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RAVEN|AMD_IS_APU},
	/* Arcturus */
	{0x1002, 0x738C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ARCTURUS},
	{0x1002, 0x7388, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ARCTURUS},
	{0x1002, 0x738E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ARCTURUS},
	{0x1002, 0x7390, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ARCTURUS},
	/* Navi10 */
	{0x1002, 0x7310, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x7312, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x7318, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x7319, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x731A, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x731B, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x731E, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	{0x1002, 0x731F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI10},
	/* Navi14 */
	{0x1002, 0x7340, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI14},
	{0x1002, 0x7341, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI14},
	{0x1002, 0x7347, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI14},
	{0x1002, 0x734F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI14},

	/* Renoir */
	{0x1002, 0x15E7, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RENOIR|AMD_IS_APU},
	{0x1002, 0x1636, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RENOIR|AMD_IS_APU},
	{0x1002, 0x1638, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RENOIR|AMD_IS_APU},
	{0x1002, 0x164C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_RENOIR|AMD_IS_APU},

	/* Navi12 */
	{0x1002, 0x7360, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI12},
	{0x1002, 0x7362, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVI12},

	/* Sienna_Cichlid */
	{0x1002, 0x73A0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A5, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73A9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73AB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73AC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73AD, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73AE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73AF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},
	{0x1002, 0x73BF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_SIENNA_CICHLID},

	/* Yellow Carp */
	{0x1002, 0x164D, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_YELLOW_CARP|AMD_IS_APU},
	{0x1002, 0x1681, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_YELLOW_CARP|AMD_IS_APU},

	/* Navy_Flounder */
	{0x1002, 0x73C0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73C1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73C3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DD, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},
	{0x1002, 0x73DF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_NAVY_FLOUNDER},

	/* DIMGREY_CAVEFISH */
	{0x1002, 0x73E0, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73E1, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73E2, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73E3, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73E8, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73E9, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73EA, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73EB, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73EC, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73ED, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73EF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},
	{0x1002, 0x73FF, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_DIMGREY_CAVEFISH},

	/* Aldebaran */
	{0x1002, 0x7408, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ALDEBARAN},
	{0x1002, 0x740C, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ALDEBARAN},
	{0x1002, 0x740F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ALDEBARAN},
	{0x1002, 0x7410, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_ALDEBARAN},

	/* CYAN_SKILLFISH */
	{0x1002, 0x13FE, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CYAN_SKILLFISH|AMD_IS_APU},
	{0x1002, 0x143F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_CYAN_SKILLFISH|AMD_IS_APU},

	/* BEIGE_GOBY */
	{0x1002, 0x7420, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},
	{0x1002, 0x7421, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},
	{0x1002, 0x7422, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},
	{0x1002, 0x7423, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},
	{0x1002, 0x7424, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},
	{0x1002, 0x743F, PCI_ANY_ID, PCI_ANY_ID, 0, 0, CHIP_BEIGE_GOBY},

	{ PCI_DEVICE(0x1002, PCI_ANY_ID),
	  .class = PCI_CLASS_DISPLAY_VGA << 8,
	  .class_mask = 0xffffff,
	  .driver_data = CHIP_IP_DISCOVERY },

	{ PCI_DEVICE(0x1002, PCI_ANY_ID),
	  .class = PCI_CLASS_DISPLAY_OTHER << 8,
	  .class_mask = 0xffffff,
	  .driver_data = CHIP_IP_DISCOVERY },

	{ PCI_DEVICE(0x1002, PCI_ANY_ID),
	  .class = PCI_CLASS_ACCELERATOR_PROCESSING << 8,
	  .class_mask = 0xffffff,
	  .driver_data = CHIP_IP_DISCOVERY },

	{0, 0, 0}
};

MODULE_DEVICE_TABLE(pci, pciidlist);

static const struct amdgpu_asic_type_quirk asic_type_quirks[] = {
	/* differentiate between P10 and P11 asics with the same DID */
	{0x67FF, 0xE3, CHIP_POLARIS10},
	{0x67FF, 0xE7, CHIP_POLARIS10},
	{0x67FF, 0xF3, CHIP_POLARIS10},
	{0x67FF, 0xF7, CHIP_POLARIS10},
};

static const struct drm_driver amdgpu_kms_driver;

static void amdgpu_get_secondary_funcs(struct amdgpu_device *adev)
{
	struct pci_dev *p = NULL;
	int i;

	/* 0 - GPU
	 * 1 - audio
	 * 2 - USB
	 * 3 - UCSI
	 */
	for (i = 1; i < 4; i++) {
		p = pci_get_domain_bus_and_slot(pci_domain_nr(adev->pdev->bus),
						adev->pdev->bus->number, i);
		if (p) {
			pm_runtime_get_sync(&p->dev);
			pm_runtime_mark_last_busy(&p->dev);
			pm_runtime_put_autosuspend(&p->dev);
			pci_dev_put(p);
		}
	}
}

static void amdgpu_init_debug_options(struct amdgpu_device *adev)
{
	if (amdgpu_debug_mask & AMDGPU_DEBUG_VM) {
		pr_info("debug: VM handling debug enabled\n");
		adev->debug_vm = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_LARGEBAR) {
		pr_info("debug: enabled simulating large-bar capability on non-large bar system\n");
		adev->debug_largebar = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_DISABLE_GPU_SOFT_RECOVERY) {
		pr_info("debug: soft reset for GPU recovery disabled\n");
		adev->debug_disable_soft_recovery = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_USE_VRAM_FW_BUF) {
		pr_info("debug: place fw in vram for frontdoor loading\n");
		adev->debug_use_vram_fw_buf = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_ENABLE_RAS_ACA) {
		pr_info("debug: enable RAS ACA\n");
		adev->debug_enable_ras_aca = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_ENABLE_EXP_RESETS) {
		pr_info("debug: enable experimental reset features\n");
		adev->debug_exp_resets = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_DISABLE_GPU_RING_RESET) {
		pr_info("debug: ring reset disabled\n");
		adev->debug_disable_gpu_ring_reset = true;
	}
	if (amdgpu_debug_mask & AMDGPU_DEBUG_SMU_POOL) {
		pr_info("debug: use vram for smu pool\n");
		adev->pm.smu_debug_mask |= SMU_DEBUG_POOL_USE_VRAM;
	}
	if (amdgpu_debug_mask & AMDGPU_DEBUG_VM_USERPTR) {
		pr_info("debug: VM mode debug for userptr is enabled\n");
		adev->debug_vm_userptr = true;
	}

	if (amdgpu_debug_mask & AMDGPU_DEBUG_DISABLE_RAS_CE_LOG) {
		pr_info("debug: disable kernel logs of correctable errors\n");
		adev->debug_disable_ce_logs = true;
	}
}

static unsigned long amdgpu_fix_asic_type(struct pci_dev *pdev, unsigned long flags)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(asic_type_quirks); i++) {
		if (pdev->device == asic_type_quirks[i].device &&
			pdev->revision == asic_type_quirks[i].revision) {
				flags &= ~AMD_ASIC_MASK;
				flags |= asic_type_quirks[i].type;
				break;
			}
	}

	return flags;
}

static int amdgpu_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *ent)
{
	struct drm_device *ddev;
	struct amdgpu_device *adev;
	unsigned long flags = ent->driver_data;
	int ret, retry = 0, i;
	bool supports_atomic = false;

	if ((pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA ||
	    (pdev->class >> 8) == PCI_CLASS_DISPLAY_OTHER) {
		if (drm_firmware_drivers_only() && amdgpu_modeset == -1)
			return -EINVAL;
	}

	/* skip devices which are owned by radeon */
	for (i = 0; i < ARRAY_SIZE(amdgpu_unsupported_pciidlist); i++) {
		if (amdgpu_unsupported_pciidlist[i] == pdev->device)
			return -ENODEV;
	}

	if (amdgpu_aspm == -1 && !pcie_aspm_enabled(pdev))
		amdgpu_aspm = 0;

	if (amdgpu_virtual_display ||
	    amdgpu_device_asic_has_dc_support(pdev, flags & AMD_ASIC_MASK))
		supports_atomic = true;

	if ((flags & AMD_EXP_HW_SUPPORT) && !amdgpu_exp_hw_support) {
		DRM_INFO("This hardware requires experimental hardware support.\n"
			 "See modparam exp_hw_support\n");
		return -ENODEV;
	}

	flags = amdgpu_fix_asic_type(pdev, flags);

	/* Due to hardware bugs, S/G Display on raven requires a 1:1 IOMMU mapping,
	 * however, SME requires an indirect IOMMU mapping because the encryption
	 * bit is beyond the DMA mask of the chip.
	 */
	if (cc_platform_has(CC_ATTR_MEM_ENCRYPT) &&
	    ((flags & AMD_ASIC_MASK) == CHIP_RAVEN)) {
		dev_info(&pdev->dev,
			 "SME is not compatible with RAVEN\n");
		return -ENOTSUPP;
	}

	switch (flags & AMD_ASIC_MASK) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_OLAND:
	case CHIP_HAINAN:
#ifdef CONFIG_DRM_AMDGPU_SI
		if (!amdgpu_si_support) {
			dev_info(&pdev->dev,
				 "SI support provided by radeon.\n");
			dev_info(&pdev->dev,
				 "Use radeon.si_support=0 amdgpu.si_support=1 to override.\n"
				);
			return -ENODEV;
		}
		break;
#else
		dev_info(&pdev->dev, "amdgpu is built without SI support.\n");
		return -ENODEV;
#endif
	case CHIP_KAVERI:
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_KABINI:
	case CHIP_MULLINS:
#ifdef CONFIG_DRM_AMDGPU_CIK
		if (!amdgpu_cik_support) {
			dev_info(&pdev->dev,
				 "CIK support provided by radeon.\n");
			dev_info(&pdev->dev,
				 "Use radeon.cik_support=0 amdgpu.cik_support=1 to override.\n"
				);
			return -ENODEV;
		}
		break;
#else
		dev_info(&pdev->dev, "amdgpu is built without CIK support.\n");
		return -ENODEV;
#endif
	default:
		break;
	}

	adev = devm_drm_dev_alloc(&pdev->dev, &amdgpu_kms_driver, typeof(*adev), ddev);
	if (IS_ERR(adev))
		return PTR_ERR(adev);

	adev->dev  = &pdev->dev;
	adev->pdev = pdev;
	ddev = adev_to_drm(adev);

	if (!supports_atomic)
		ddev->driver_features &= ~DRIVER_ATOMIC;

	ret = pci_enable_device(pdev);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, ddev);

	amdgpu_init_debug_options(adev);

	ret = amdgpu_driver_load_kms(adev, flags);
	if (ret)
		goto err_pci;

retry_init:
	ret = drm_dev_register(ddev, flags);
	if (ret == -EAGAIN && ++retry <= 3) {
		DRM_INFO("retry init %d\n", retry);
		/* Don't request EX mode too frequently which is attacking */
		msleep(5000);
		goto retry_init;
	} else if (ret) {
		goto err_pci;
	}

	ret = amdgpu_xcp_dev_register(adev, ent);
	if (ret)
		goto err_pci;

	ret = amdgpu_amdkfd_drm_client_create(adev);
	if (ret)
		goto err_pci;

	/*
	 * 1. don't init fbdev on hw without DCE
	 * 2. don't init fbdev if there are no connectors
	 */
	if (adev->mode_info.mode_config_initialized &&
	    !list_empty(&adev_to_drm(adev)->mode_config.connector_list)) {
		const struct drm_format_info *format;

		/* select 8 bpp console on low vram cards */
		if (adev->gmc.real_vram_size <= (32*1024*1024))
			format = drm_format_info(DRM_FORMAT_C8);
		else
			format = NULL;

		drm_client_setup(adev_to_drm(adev), format);
	}

	ret = amdgpu_debugfs_init(adev);
	if (ret)
		DRM_ERROR("Creating debugfs files failed (%d).\n", ret);

	if (adev->pm.rpm_mode != AMDGPU_RUNPM_NONE) {
		/* only need to skip on ATPX */
		if (amdgpu_device_supports_px(adev))
			dev_pm_set_driver_flags(ddev->dev, DPM_FLAG_NO_DIRECT_COMPLETE);
		/* we want direct complete for BOCO */
		if (amdgpu_device_supports_boco(adev))
			dev_pm_set_driver_flags(ddev->dev, DPM_FLAG_SMART_PREPARE |
						DPM_FLAG_SMART_SUSPEND |
						DPM_FLAG_MAY_SKIP_RESUME);
		pm_runtime_use_autosuspend(ddev->dev);
		pm_runtime_set_autosuspend_delay(ddev->dev, 5000);

		pm_runtime_allow(ddev->dev);

		pm_runtime_mark_last_busy(ddev->dev);
		pm_runtime_put_autosuspend(ddev->dev);

		pci_wake_from_d3(pdev, TRUE);

		/*
		 * For runpm implemented via BACO, PMFW will handle the
		 * timing for BACO in and out:
		 *   - put ASIC into BACO state only when both video and
		 *     audio functions are in D3 state.
		 *   - pull ASIC out of BACO state when either video or
		 *     audio function is in D0 state.
		 * Also, at startup, PMFW assumes both functions are in
		 * D0 state.
		 *
		 * So if snd driver was loaded prior to amdgpu driver
		 * and audio function was put into D3 state, there will
		 * be no PMFW-aware D-state transition(D0->D3) on runpm
		 * suspend. Thus the BACO will be not correctly kicked in.
		 *
		 * Via amdgpu_get_secondary_funcs(), the audio dev is put
		 * into D0 state. Then there will be a PMFW-aware D-state
		 * transition(D0->D3) on runpm suspend.
		 */
		if (amdgpu_device_supports_baco(adev) &&
		    !(adev->flags & AMD_IS_APU) &&
		    adev->asic_type >= CHIP_NAVI10)
			amdgpu_get_secondary_funcs(adev);
	}

	return 0;

err_pci:
	pci_disable_device(pdev);
	return ret;
}

static void
amdgpu_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(dev);

	amdgpu_ras_eeprom_check_and_recover(adev);
	amdgpu_xcp_dev_unplug(adev);
	amdgpu_gmc_prepare_nps_mode_change(adev);
	drm_dev_unplug(dev);

	if (adev->pm.rpm_mode != AMDGPU_RUNPM_NONE) {
		pm_runtime_get_sync(dev->dev);
		pm_runtime_forbid(dev->dev);
	}

	amdgpu_driver_unload_kms(dev);

	/*
	 * Flush any in flight DMA operations from device.
	 * Clear the Bus Master Enable bit and then wait on the PCIe Device
	 * StatusTransactions Pending bit.
	 */
	pci_disable_device(pdev);
	pci_wait_for_pending_transaction(pdev);
}

static void
amdgpu_pci_shutdown(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(dev);

	if (amdgpu_ras_intr_triggered())
		return;

	/* device maybe not resumed here, return immediately in this case */
	if (adev->in_s4 && adev->in_suspend)
		return;

	/* if we are running in a VM, make sure the device
	 * torn down properly on reboot/shutdown.
	 * unfortunately we can't detect certain
	 * hypervisors so just do this all the time.
	 */
	if (!amdgpu_passthrough(adev))
		adev->mp1_state = PP_MP1_STATE_UNLOAD;
	amdgpu_device_ip_suspend(adev);
	adev->mp1_state = PP_MP1_STATE_NONE;
}

static int amdgpu_pmops_prepare(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	/* device maybe not resumed here, return immediately in this case */
	if (adev->in_s4 && adev->in_suspend)
		return 0;

	/* Return a positive number here so
	 * DPM_FLAG_SMART_SUSPEND works properly
	 */
	if (amdgpu_device_supports_boco(adev) && pm_runtime_suspended(dev))
		return 1;

	/* if we will not support s3 or s2i for the device
	 *  then skip suspend
	 */
	if (!amdgpu_acpi_is_s0ix_active(adev) &&
	    !amdgpu_acpi_is_s3_active(adev))
		return 1;

	return amdgpu_device_prepare(drm_dev);
}

static void amdgpu_pmops_complete(struct device *dev)
{
	amdgpu_device_complete(dev_get_drvdata(dev));
}

static int amdgpu_pmops_suspend(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	if (amdgpu_acpi_is_s0ix_active(adev))
		adev->in_s0ix = true;
	else if (amdgpu_acpi_is_s3_active(adev))
		adev->in_s3 = true;
	if (!adev->in_s0ix && !adev->in_s3) {
		/* don't allow going deep first time followed by s2idle the next time */
		if (adev->last_suspend_state != PM_SUSPEND_ON &&
		    adev->last_suspend_state != pm_suspend_target_state) {
			drm_err_once(drm_dev, "Unsupported suspend state %d\n",
				     pm_suspend_target_state);
			return -EINVAL;
		}
		return 0;
	}

	/* cache the state last used for suspend */
	adev->last_suspend_state = pm_suspend_target_state;

	return amdgpu_device_suspend(drm_dev, true);
}

static int amdgpu_pmops_suspend_noirq(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	if (amdgpu_acpi_should_gpu_reset(adev))
		return amdgpu_asic_reset(adev);

	return 0;
}

static int amdgpu_pmops_resume(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	int r;

	if (!adev->in_s0ix && !adev->in_s3)
		return 0;

	/* Avoids registers access if device is physically gone */
	if (!pci_device_is_present(adev->pdev))
		adev->no_hw_access = true;

	r = amdgpu_device_resume(drm_dev, true);
	if (amdgpu_acpi_is_s0ix_active(adev))
		adev->in_s0ix = false;
	else
		adev->in_s3 = false;
	return r;
}

static int amdgpu_pmops_freeze(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	int r;

	r = amdgpu_device_suspend(drm_dev, true);
	if (r)
		return r;

	if (amdgpu_acpi_should_gpu_reset(adev))
		return amdgpu_asic_reset(adev);
	return 0;
}

static int amdgpu_pmops_thaw(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	/* do not resume device if it's normal hibernation */
	if (!pm_hibernate_is_recovering())
		return 0;

	return amdgpu_device_resume(drm_dev, true);
}

static int amdgpu_pmops_poweroff(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	/* device maybe not resumed here, return immediately in this case */
	if (adev->in_s4 && adev->in_suspend)
		return 0;

	return amdgpu_device_suspend(drm_dev, true);
}

static int amdgpu_pmops_restore(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);

	return amdgpu_device_resume(drm_dev, true);
}

static int amdgpu_runtime_idle_check_display(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);

	if (adev->mode_info.num_crtc) {
		struct drm_connector *list_connector;
		struct drm_connector_list_iter iter;
		int ret = 0;

		if (amdgpu_runtime_pm != -2) {
			/* XXX: Return busy if any displays are connected to avoid
			 * possible display wakeups after runtime resume due to
			 * hotplug events in case any displays were connected while
			 * the GPU was in suspend.  Remove this once that is fixed.
			 */
			mutex_lock(&drm_dev->mode_config.mutex);
			drm_connector_list_iter_begin(drm_dev, &iter);
			drm_for_each_connector_iter(list_connector, &iter) {
				if (list_connector->status == connector_status_connected) {
					ret = -EBUSY;
					break;
				}
			}
			drm_connector_list_iter_end(&iter);
			mutex_unlock(&drm_dev->mode_config.mutex);

			if (ret)
				return ret;
		}

		if (adev->dc_enabled) {
			struct drm_crtc *crtc;

			drm_for_each_crtc(crtc, drm_dev) {
				drm_modeset_lock(&crtc->mutex, NULL);
				if (crtc->state->active)
					ret = -EBUSY;
				drm_modeset_unlock(&crtc->mutex);
				if (ret < 0)
					break;
			}
		} else {
			mutex_lock(&drm_dev->mode_config.mutex);
			drm_modeset_lock(&drm_dev->mode_config.connection_mutex, NULL);

			drm_connector_list_iter_begin(drm_dev, &iter);
			drm_for_each_connector_iter(list_connector, &iter) {
				if (list_connector->dpms ==  DRM_MODE_DPMS_ON) {
					ret = -EBUSY;
					break;
				}
			}

			drm_connector_list_iter_end(&iter);

			drm_modeset_unlock(&drm_dev->mode_config.connection_mutex);
			mutex_unlock(&drm_dev->mode_config.mutex);
		}
		if (ret)
			return ret;
	}

	return 0;
}

static int amdgpu_runtime_idle_check_userq(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	struct amdgpu_usermode_queue *queue;
	struct amdgpu_userq_mgr *uqm, *tmp;
	int queue_id;
	int ret = 0;

	mutex_lock(&adev->userq_mutex);
	list_for_each_entry_safe(uqm, tmp, &adev->userq_mgr_list, list) {
		idr_for_each_entry(&uqm->userq_idr, queue, queue_id) {
			ret = -EBUSY;
			goto done;
		}
	}
done:
	mutex_unlock(&adev->userq_mutex);

	return ret;
}

static int amdgpu_pmops_runtime_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	int ret, i;

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_NONE) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	ret = amdgpu_runtime_idle_check_display(dev);
	if (ret)
		return ret;
	ret = amdgpu_runtime_idle_check_userq(dev);
	if (ret)
		return ret;

	/* wait for all rings to drain before suspending */
	for (i = 0; i < AMDGPU_MAX_RINGS; i++) {
		struct amdgpu_ring *ring = adev->rings[i];

		if (ring && ring->sched.ready) {
			ret = amdgpu_fence_wait_empty(ring);
			if (ret)
				return -EBUSY;
		}
	}

	adev->in_runpm = true;
	if (adev->pm.rpm_mode == AMDGPU_RUNPM_PX)
		drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

	/*
	 * By setting mp1_state as PP_MP1_STATE_UNLOAD, MP1 will do some
	 * proper cleanups and put itself into a state ready for PNP. That
	 * can address some random resuming failure observed on BOCO capable
	 * platforms.
	 * TODO: this may be also needed for PX capable platform.
	 */
	if (adev->pm.rpm_mode == AMDGPU_RUNPM_BOCO)
		adev->mp1_state = PP_MP1_STATE_UNLOAD;

	ret = amdgpu_device_prepare(drm_dev);
	if (ret)
		return ret;
	ret = amdgpu_device_suspend(drm_dev, false);
	if (ret) {
		adev->in_runpm = false;
		if (adev->pm.rpm_mode == AMDGPU_RUNPM_BOCO)
			adev->mp1_state = PP_MP1_STATE_NONE;
		return ret;
	}

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_BOCO)
		adev->mp1_state = PP_MP1_STATE_NONE;

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_PX) {
		/* Only need to handle PCI state in the driver for ATPX
		 * PCI core handles it for _PR3.
		 */
		amdgpu_device_cache_pci_state(pdev);
		pci_disable_device(pdev);
		pci_ignore_hotplug(pdev);
		pci_set_power_state(pdev, PCI_D3cold);
		drm_dev->switch_power_state = DRM_SWITCH_POWER_DYNAMIC_OFF;
	} else if (adev->pm.rpm_mode == AMDGPU_RUNPM_BOCO) {
		/* nothing to do */
	} else if ((adev->pm.rpm_mode == AMDGPU_RUNPM_BACO) ||
			(adev->pm.rpm_mode == AMDGPU_RUNPM_BAMACO)) {
		amdgpu_device_baco_enter(adev);
	}

	dev_dbg(&pdev->dev, "asic/device is runtime suspended\n");

	return 0;
}

static int amdgpu_pmops_runtime_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct drm_device *drm_dev = pci_get_drvdata(pdev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	int ret;

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_NONE)
		return -EINVAL;

	/* Avoids registers access if device is physically gone */
	if (!pci_device_is_present(adev->pdev))
		adev->no_hw_access = true;

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_PX) {
		drm_dev->switch_power_state = DRM_SWITCH_POWER_CHANGING;

		/* Only need to handle PCI state in the driver for ATPX
		 * PCI core handles it for _PR3.
		 */
		pci_set_power_state(pdev, PCI_D0);
		amdgpu_device_load_pci_state(pdev);
		ret = pci_enable_device(pdev);
		if (ret)
			return ret;
		pci_set_master(pdev);
	} else if (adev->pm.rpm_mode == AMDGPU_RUNPM_BOCO) {
		/* Only need to handle PCI state in the driver for ATPX
		 * PCI core handles it for _PR3.
		 */
		pci_set_master(pdev);
	} else if ((adev->pm.rpm_mode == AMDGPU_RUNPM_BACO) ||
			(adev->pm.rpm_mode == AMDGPU_RUNPM_BAMACO)) {
		amdgpu_device_baco_exit(adev);
	}
	ret = amdgpu_device_resume(drm_dev, false);
	if (ret) {
		if (adev->pm.rpm_mode == AMDGPU_RUNPM_PX)
			pci_disable_device(pdev);
		return ret;
	}

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_PX)
		drm_dev->switch_power_state = DRM_SWITCH_POWER_ON;
	adev->in_runpm = false;
	return 0;
}

static int amdgpu_pmops_runtime_idle(struct device *dev)
{
	struct drm_device *drm_dev = dev_get_drvdata(dev);
	struct amdgpu_device *adev = drm_to_adev(drm_dev);
	int ret;

	if (adev->pm.rpm_mode == AMDGPU_RUNPM_NONE) {
		pm_runtime_forbid(dev);
		return -EBUSY;
	}

	ret = amdgpu_runtime_idle_check_display(dev);
	if (ret)
		goto done;

	ret = amdgpu_runtime_idle_check_userq(dev);
done:
	pm_runtime_mark_last_busy(dev);
	pm_runtime_autosuspend(dev);
	return ret;
}

static int amdgpu_drm_release(struct inode *inode, struct file *filp)
{
	struct drm_file *file_priv = filp->private_data;
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;

	if (fpriv) {
		fpriv->evf_mgr.fd_closing = true;
		amdgpu_eviction_fence_destroy(&fpriv->evf_mgr);
		amdgpu_userq_mgr_fini(&fpriv->userq_mgr);
	}

	return drm_release(inode, filp);
}

long amdgpu_drm_ioctl(struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	struct drm_device *dev;
	long ret;

	dev = file_priv->minor->dev;
	ret = pm_runtime_get_sync(dev->dev);
	if (ret < 0)
		goto out;

	ret = drm_ioctl(filp, cmd, arg);

	pm_runtime_mark_last_busy(dev->dev);
out:
	pm_runtime_put_autosuspend(dev->dev);
	return ret;
}

static const struct dev_pm_ops amdgpu_pm_ops = {
	.prepare = amdgpu_pmops_prepare,
	.complete = amdgpu_pmops_complete,
	.suspend = amdgpu_pmops_suspend,
	.suspend_noirq = amdgpu_pmops_suspend_noirq,
	.resume = amdgpu_pmops_resume,
	.freeze = amdgpu_pmops_freeze,
	.thaw = amdgpu_pmops_thaw,
	.poweroff = amdgpu_pmops_poweroff,
	.restore = amdgpu_pmops_restore,
	.runtime_suspend = amdgpu_pmops_runtime_suspend,
	.runtime_resume = amdgpu_pmops_runtime_resume,
	.runtime_idle = amdgpu_pmops_runtime_idle,
};

static int amdgpu_flush(struct file *f, fl_owner_t id)
{
	struct drm_file *file_priv = f->private_data;
	struct amdgpu_fpriv *fpriv = file_priv->driver_priv;
	long timeout = MAX_WAIT_SCHED_ENTITY_Q_EMPTY;

	timeout = amdgpu_ctx_mgr_entity_flush(&fpriv->ctx_mgr, timeout);
	timeout = amdgpu_vm_wait_idle(&fpriv->vm, timeout);

	return timeout >= 0 ? 0 : timeout;
}

static const struct file_operations amdgpu_driver_kms_fops = {
	.owner = THIS_MODULE,
	.open = drm_open,
	.flush = amdgpu_flush,
	.release = amdgpu_drm_release,
	.unlocked_ioctl = amdgpu_drm_ioctl,
	.mmap = drm_gem_mmap,
	.poll = drm_poll,
	.read = drm_read,
#ifdef CONFIG_COMPAT
	.compat_ioctl = amdgpu_kms_compat_ioctl,
#endif
#ifdef CONFIG_PROC_FS
	.show_fdinfo = drm_show_fdinfo,
#endif
	.fop_flags = FOP_UNSIGNED_OFFSET,
};

int amdgpu_file_to_fpriv(struct file *filp, struct amdgpu_fpriv **fpriv)
{
	struct drm_file *file;

	if (!filp)
		return -EINVAL;

	if (filp->f_op != &amdgpu_driver_kms_fops)
		return -EINVAL;

	file = filp->private_data;
	*fpriv = file->driver_priv;
	return 0;
}

const struct drm_ioctl_desc amdgpu_ioctls_kms[] = {
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_CREATE, amdgpu_gem_create_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_CTX, amdgpu_ctx_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_VM, amdgpu_vm_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_SCHED, amdgpu_sched_ioctl, DRM_MASTER),
	DRM_IOCTL_DEF_DRV(AMDGPU_BO_LIST, amdgpu_bo_list_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_FENCE_TO_HANDLE, amdgpu_cs_fence_to_handle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	/* KMS */
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_MMAP, amdgpu_gem_mmap_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_WAIT_IDLE, amdgpu_gem_wait_idle_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_CS, amdgpu_cs_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_INFO, amdgpu_info_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_WAIT_CS, amdgpu_cs_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_WAIT_FENCES, amdgpu_cs_wait_fences_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_METADATA, amdgpu_gem_metadata_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_VA, amdgpu_gem_va_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_OP, amdgpu_gem_op_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_GEM_USERPTR, amdgpu_gem_userptr_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_USERQ, amdgpu_userq_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_USERQ_SIGNAL, amdgpu_userq_signal_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(AMDGPU_USERQ_WAIT, amdgpu_userq_wait_ioctl, DRM_AUTH|DRM_RENDER_ALLOW),
};

static const struct drm_driver amdgpu_kms_driver = {
	.driver_features =
	    DRIVER_ATOMIC |
	    DRIVER_GEM |
	    DRIVER_RENDER | DRIVER_MODESET | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.open = amdgpu_driver_open_kms,
	.postclose = amdgpu_driver_postclose_kms,
	.ioctls = amdgpu_ioctls_kms,
	.num_ioctls = ARRAY_SIZE(amdgpu_ioctls_kms),
	.dumb_create = amdgpu_mode_dumb_create,
	.dumb_map_offset = amdgpu_mode_dumb_mmap,
	DRM_FBDEV_TTM_DRIVER_OPS,
	.fops = &amdgpu_driver_kms_fops,
	.release = &amdgpu_driver_release_kms,
#ifdef CONFIG_PROC_FS
	.show_fdinfo = amdgpu_show_fdinfo,
#endif

	.gem_prime_import = amdgpu_gem_prime_import,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

const struct drm_driver amdgpu_partition_driver = {
	.driver_features =
	    DRIVER_GEM | DRIVER_RENDER | DRIVER_SYNCOBJ |
	    DRIVER_SYNCOBJ_TIMELINE,
	.open = amdgpu_driver_open_kms,
	.postclose = amdgpu_driver_postclose_kms,
	.ioctls = amdgpu_ioctls_kms,
	.num_ioctls = ARRAY_SIZE(amdgpu_ioctls_kms),
	.dumb_create = amdgpu_mode_dumb_create,
	.dumb_map_offset = amdgpu_mode_dumb_mmap,
	DRM_FBDEV_TTM_DRIVER_OPS,
	.fops = &amdgpu_driver_kms_fops,
	.release = &amdgpu_driver_release_kms,

	.gem_prime_import = amdgpu_gem_prime_import,

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.major = KMS_DRIVER_MAJOR,
	.minor = KMS_DRIVER_MINOR,
	.patchlevel = KMS_DRIVER_PATCHLEVEL,
};

static struct pci_error_handlers amdgpu_pci_err_handler = {
	.error_detected	= amdgpu_pci_error_detected,
	.mmio_enabled	= amdgpu_pci_mmio_enabled,
	.slot_reset	= amdgpu_pci_slot_reset,
	.resume		= amdgpu_pci_resume,
};

static const struct attribute_group *amdgpu_sysfs_groups[] = {
	&amdgpu_vram_mgr_attr_group,
	&amdgpu_gtt_mgr_attr_group,
	&amdgpu_flash_attr_group,
	NULL,
};

static struct pci_driver amdgpu_kms_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = pciidlist,
	.probe = amdgpu_pci_probe,
	.remove = amdgpu_pci_remove,
	.shutdown = amdgpu_pci_shutdown,
	.driver.pm = &amdgpu_pm_ops,
	.err_handler = &amdgpu_pci_err_handler,
	.dev_groups = amdgpu_sysfs_groups,
};

static int __init amdgpu_init(void)
{
	int r;

	r = amdgpu_sync_init();
	if (r)
		goto error_sync;

	r = amdgpu_userq_fence_slab_init();
	if (r)
		goto error_fence;

	DRM_INFO("amdgpu kernel modesetting enabled.\n");
	amdgpu_register_atpx_handler();
	amdgpu_acpi_detect();

	/* Ignore KFD init failures. Normal when CONFIG_HSA_AMD is not set. */
	amdgpu_amdkfd_init();

	if (amdgpu_pp_feature_mask & PP_OVERDRIVE_MASK) {
		add_taint(TAINT_CPU_OUT_OF_SPEC, LOCKDEP_STILL_OK);
		pr_crit("Overdrive is enabled, please disable it before "
			"reporting any bugs unrelated to overdrive.\n");
	}

	/* let modprobe override vga console setting */
	return pci_register_driver(&amdgpu_kms_pci_driver);

error_fence:
	amdgpu_sync_fini();

error_sync:
	return r;
}

static void __exit amdgpu_exit(void)
{
	amdgpu_amdkfd_fini();
	pci_unregister_driver(&amdgpu_kms_pci_driver);
	amdgpu_unregister_atpx_handler();
	amdgpu_acpi_release();
	amdgpu_sync_fini();
	amdgpu_userq_fence_slab_fini();
	mmu_notifier_synchronize();
	amdgpu_xcp_drv_release();
}

module_init(amdgpu_init);
module_exit(amdgpu_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
