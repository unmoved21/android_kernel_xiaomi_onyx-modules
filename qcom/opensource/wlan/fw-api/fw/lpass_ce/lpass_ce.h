/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all
 * copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 * WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 * AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/** @file
 * This file specifies the LPASS CE interface for the xpan Software
 * Architecture.
 *
 * It includes definitions of CE Tx, Rx msg format.
 */

#ifndef _LPASS_CE_H_
#define _LPASS_CE_H_

#include <a_types.h>

/* Size of the TLV Header which contains Tag and Length fields */
#define LPASSCE_TLV_HDR_SIZE   (1 * sizeof(A_UINT32))

/**
 * TLV Helper macro to get the TLV Header given the pointer
 *  to the TLV buffer.
 */
#define LPASSCETLV_GET_HDR(tlv_buf)        (((A_UINT32 *)(tlv_buf))[0])

/**
 * TLV Helper macro to set the TLV Header given the pointer
 * to the TLV buffer.
 */
#define LPASSCETLV_SET_HDR(tlv_buf, tag, len) \
    ((((A_UINT32 *)(tlv_buf))[0]) = (((tag) << 16) | ((len) & 0x0000FFFF)))
/** TLV Helper macro to get the TLV Tag given the TLV header. */
#define LPASSCETLV_GET_TLVTAG(tlv_header)  ((A_UINT32)((tlv_header)>>16))

/**
 * TLV Helper macro to get the TLV Buffer Length (minus TLV header size)
 * given the TLV header.
 */
#define LPASSCETLV_GET_TLVLEN(tlv_header) \
    ((A_UINT32)((tlv_header) & 0x0000FFFF))

/**
 * TLV Helper macro to get the TLV length from TLV structure size by
 * removing TLV header size.
 */
#define LPASSCETLV_GET_STRUCT_TLVLEN(tlv_struct) \
    ((A_UINT32)(sizeof(tlv_struct)-LPASSCE_TLV_HDR_SIZE))

/*
 * CE TX MSG FORMAT
 */

typedef struct {
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
     A_UINT32 msg_type;   /* XPAN msg type */
     A_UINT32 msg_id: 16, /* sequence number of the message for debug purposes */   //[15:0]
              dbg_indx  : 16;   //[31:16]
#else
     A_UINT32 msg_type;
     A_UINT32 dbg_indx  : 16,   //[31:16]
              msg_id: 16;   //[15:0]
#endif
} xpan_msg_hdr;

typedef enum {
    /* TX audio message */
    XPAN_TX_AUDIO_PACKET,
    /* Tx completion msg */
    XPAN_TX_COMPLETION,
    /* Rx indication msg */
    XPAN_RX_INDICATION,
    /* Peer stats msg */
    XPAN_PEER_STATS,
    /* direct link up msg from CE driver */
    XPAN_DIRECT_LINK_UP_INDICATION,
    /* direct link down req from CE driver */
    XPAN_DIRECT_LINK_DOWN_REQUEST, /* correct name */
        /* DEPRECATED:
         * For backwards compatibility, maintain old name as an alias
         * for the new name
         */
        XPAN_DIRECT_LINK_DOWN_INDICATION = XPAN_DIRECT_LINK_DOWN_REQUEST,
    XPAN_DIRECT_LINK_DOWN_RESPONSE,
    XPAN_MODE,
    XPAN_SET_BURST_INTERVAL,
    XPAN_SET_RX_CCE_FILTER,
} XPAN_MSG_TYPE_T;

typedef enum {
    LPASSCETLV_TAG_STRUC_xpan_tx_audio_fixed_param,
    LPASSCETLV_TAG_STRUC_xpan_tx_buffer_info_array,
    LPASSCETLV_TAG_STRUC_xpan_tx_completion_info_array,
    LPASSCETLV_TAG_STRUC_xpan_tx_completion_fixed_param,
    LPASSCETLV_TAG_STRUC_xpan_rx_audio_fixed_param,
    LPASSCETLV_TAG_STRUC_xpan_rx_buffer_info_array,
    LPASSCETLV_TAG_STRUC_xpan_peer_stats,
    LPASSCETLV_TAG_STRUC_xpan_peer_stats_fixed_params,
    LPASSCETLV_TAG_STRUC_xpan_mode_fixed_param,
    LPASSCETLV_TAG_STRUC_xpan_set_burst_interval_fixed_param,
    LPASSCETLV_TAG_STRUC_xpan_set_rx_cce_filter_fixed_param,
} LPASSCETLV_TAG;

typedef enum {
    XPAN_UDP_OVER_IPV4_CHECKSUM_EN,
    XPAN_UDP_OVER_IPV6_CHECKSUM_EN,
    XPAN_NO_CHECKSUM_EN,
} XPAN_CHECKSUM_ENABLE;

typedef struct {
    /*
     * TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_tx_buffer_info_array
     * A_UINT32 tlv_tag: 16,   MSB 16 bits is TLV tag
     *          tlv_len: 16;   LSB 16 bits is TLV len
     */
    A_UINT32 tlv_hdr;
    A_UINT32 buffer_addr_lo;      //[31:0]
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 buffer_addr_hi: 8,   //[7:0]
             sw_cookie     : 10,  /*[17:8] Used for tx completion handling to identify the appropriate buffer */
             buffer_len    : 14;  //[31:18]
    A_UINT32 dest_mac_addr_31_0;  /*[31:0] 4 LSB of the destination mac address */
    A_UINT32 dest_mac_addr_47_32: 16, /*[15:0] 2 MSB of the destination mac address */
             qos                : 2,  /*[17:16] 0 – BE, 1 – BK, 2 – VI, 3 – VO */
             checksum_en        : 2,  /*[19:18] XPAN_CHECKSUM_ENABLE */
             is_non_audio_pkt   : 1,  /*[20] This is to differentiate between audio and non-audio (TSF/DTLS) */
             rsvd               : 11; //[31:21]
#else
    A_UINT32 buffer_len    : 14,  //[31:18]
             sw_cookie     : 10,  //[17:8]
             buffer_addr_hi: 8;   //[7:0]
    A_UINT32 dest_mac_addr_31_0;  //[31:0]
    A_UINT32 rsvd               : 11, //[31:21]
             is_non_audio_pkt   : 1,  /*[20] This is to differentiate between audio and non-audio (TSF/DTLS) */
             checksum_en        : 2,  /*[19:18] XPAN_CHECKSUM_ENABLE */
             qos                : 2,  //[17:16]
             dest_mac_addr_47_32: 16; //[15:0]
#endif
    /*
     * Lower 32bits of TTP value in TSF, TTP is time to play
     * An instant in time at which the RTP packet (audio) should
     * be transmitted.
     */
    A_UINT32 ttp_lo;      //[31:0]
    /* Upper 32bits of TTP value in TSF */
    A_UINT32 ttp_hi;      //[31:0]
} xpan_tx_buffer_info;

typedef struct {
    A_UINT32 tlv_hdr; /* tag = LPASSCETLV_TAG_STRUC_xpan_tx_audio_fixed_param */
    /* 2 LSB of the source mac address */
    A_UINT32 src_mac_addr_31_0; //[31:0]
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    /* 4 MSB of the source mac address */
    A_UINT32 src_mac_addr_47_32 : 16, //[15:0]
             num_tx_buffers     : 16; /*[31:16] Number of buffers following this fixed param TLV */
    /*
     * Following this is the TLV
     * struct xpan_tx_buffer_info[num_tx_buffers];
     */
#else
    A_UINT32 num_tx_buffers:    : 16, //[31:16]
             src_mac_addr_47_32 : 16; //[15:0]
#endif
} xpan_tx_audio_fixed_param;

/*
 * CE TX COMPLETION AND RX MSG FORMAT
 */
typedef enum {
    XPAN_PKT_TX_SUCCESS,
    XPAN_PKT_TX_FAILURE,
    XPAN_PKT_DROP,
} XPAN_TX_STATUS;

typedef struct {
    A_UINT32 tlv_hdr; /* tag = LPASSCETLV_TAG_STRUC_xpan_tx_completion_info_array */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 sw_cookie  : 10,   /* Cookie to identify the appropriate buffer for which tx completion is received [9:0]*/
             tx_status  : 6,    /* Transmission status [15:10]*/
             mcs        : 5,    /* MCS rate [20:16]*/
             retry_count: 4,    /* Tx retry count [24:21]*/
             rsvd       : 7;    //[31:25]
#else
    A_UINT32 rsvd       : 7,   //[31:25]
             retry_count: 4,   //[24:21]
             mcs        : 5,   //[20:16]
             tx_status  : 6,   //[15:10]
             sw_cookie  : 10;  //[9:0]
             /*
              * For TX buffers, sw_cookie is calculated by LPASS for each
              * buffer based on the page num and offset.
              * This sw_cookie value is sent to FW which gets sent back to
              * LPASS by FW on OTA completions. tx sw_cookie is used by LPASS
              * to do IOVA->VA translation quickly for the buffer which got
              * transmitted.
              */
#endif
    /*
     * Below 64 bit variable is the PPDU transmission start TSF which is
     * expected to be in terms of TSF of SAP.
     * This is used in WHC for round trip time calculations.
     */
    A_UINT32 ppdu_tx_tsf_lo;
    A_UINT32 ppdu_tx_tsf_hi;
} xpan_tx_completion_info;

typedef struct {
    A_UINT32 tlv_hdr;         /* tag = LPASSCETLV_TAG_STRUC_xpan_tx_completion_fixed_param */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 num_tx_comp: 16, /* Number of tx completions following this fixed param TLV [15:0] */
             rsvd       : 16; //[31:16]
    /*
     * Following this is the TLV
     * struct xpan_tx_completion_info[num_tx_comp];
     */
#else
    A_UINT32 rsvd       : 16, //[31:16]
             num_tx_comp: 16; //[15:0]
#endif
} xpan_tx_completion_fixed_param;

typedef struct {
    xpan_msg_hdr msg_hdr;
    xpan_tx_completion_fixed_param tx_completion_fixed_param;
} xpan_tx_completion_indication_msg;

typedef struct {
    A_UINT32 tlv_hdr; /* tag = LPASSCETLV_TAG_STRUC_xpan_rx_buffer_info_array */
    A_UINT32 buffer_addr_lo;   /* Lower 32bits of the buffer dma addr [31:0] */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 buffer_addr_hi: 8,  /* Upper 8 bits of the buffer dma addr [7:0] */
             buffer_len    : 16, /* length of the buffer [23:8] */
             rsvd          : 8;  //[31:24]
#else
    A_UINT32 rsvd          : 8, //[31:24]
             buffer_len   : 16, //[23:8]
             buffer_addr_hi: 8; //[7:0]
#endif
    /*
     * Rx buffers are always provided/replenished by Host driver on APSS
     * and the sw_cookie information is used by Host driver to get the VA
     * corresponding to the sw rx descriptor which holds the skb/buffer
     * information. This sw_cookie information is needed by LPASS CE driver
     * to be used when replenishing the buffer to FW since this buffer can
     * end up at Host driver at a later point at which sw_cookie information
     * needs to be valid. (The sw_cookie information is not used by LPASS
     * for rx buffers.)
     */
    A_UINT32 sw_cookie;
} xpan_rx_buffer_info;

typedef struct {
    A_UINT32 tlv_hdr; /* tag = LPASSCETLV_TAG_STRUC_xpan_rx_audio_fixed_param */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 num_rx_buffers: 16, /* Number of buffers following this fixed param TLV [15:0] */
             rsvd          : 16; //[31:16]
    /*
     * Following this is the TLV
     * struct xpan_rx_buffer_info[num_rx_buffers];
     */
#else
    A_UINT32 rsvd          : 16, //[31:16]
             num_rx_buffers: 16; //[15:0]
    /*
     * Following this is the TLV
     * struct btoip_rx_buffer_info[num_rx_buffers];
     */
#endif
} xpan_rx_audio_fixed_param;

typedef struct {
    xpan_msg_hdr msg_hdr;
    xpan_rx_audio_fixed_param rx_audio_fixed_param;
} xpan_rx_indication_msg;

/*
 * CE PEER STATS MSG FORMAT
 */
/** 2 word representation of MAC addr */
typedef struct {
    /** upper 4 bytes of MAC address */
    A_UINT32 mac_addr31to0;
    /** lower 2 bytes of MAC address */
    A_UINT32 mac_addr47to32;
} ce_mac_addr;

typedef struct {
    A_UINT32 tlv_header; /* TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_peer_stats */
    /* peer mac address */
    ce_mac_addr mac_addr;
    /* last tx_rate in Kbps */
    A_UINT32 tx_rate;
    /* channel_access_time in us */
    A_UINT32 channel_access_time;
    /* Average rssi_ack in dBm */
    A_INT32 rssi_ack;
    /* Number of packets retried */
    A_UINT32 tx_retries;
    /* Packets flushed due to TTL */
    A_UINT32 packets_flushed;
    /* TWT dialog ID */
    A_UINT32 dialog_id;
    /* Number of TWT SP's */
    A_UINT32 num_sp_cycles;
    /* Average SP time */
    A_UINT32 avg_sp_dur_us;
    /* Minimum SP time */
    A_UINT32 min_sp_dur_us;
    /* Maximum SP time */
    A_UINT32 max_sp_dur_us;
    /* Average pkts tx per SP */
    A_UINT32 tx_mpdu_per_sp;
    /* Average pkts rx per SP */
    A_UINT32 rx_mpdu_per_sp;
    /* Average tx bytes per SP */
    A_UINT32 tx_bytes_per_sp;
    /* Average rx bytes per SP */
    A_UINT32 rx_bytes_per_sp;
    /* Current FW Qtimer bits 31:0 */
    A_UINT32 current_fw_qtimer_low;
    /* Current FW Qtimer bits 63:32 */
    A_UINT32 current_fw_qtimer_high;
    /* Current SAP TSF time bits 31:0 */
    A_UINT32 current_fw_tsf_low;
    /* Current SAP TSF time bits 63:32 */
    A_UINT32 current_fw_tsf_high;
    /* Next TWT SP start TSF time bits 31:0 */
    A_UINT32 next_twt_start_tsf_low;
    /* Next TWT SP start TSF time bits 63:32 */
    A_UINT32 next_twt_start_tsf_high;
    /* Current STA TSF time bits 31:0 */
    A_UINT32 current_fw_sta_tsf_low;
    /* Current STA TSF time bits 63:32 */
    A_UINT32 current_fw_sta_tsf_high;
    /* total no.of pkts to transmit over OTA in current service period */
    A_UINT32 total_tx_mpdu_per_sp;
    A_INT8 is_aggregation_enabled; /* 0 -> disabled, 1 -> enabled */
} xpan_peer_stats;

typedef struct {
    A_UINT32 tlv_hdr; /* TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_peer_stats_fixed_params */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 num_peer_stats: 16, //[15:0]
             rsvd          : 16; //[31:16]
     /*
      * Following this are the stats TLVs
      * struct xpan_wlan_stats repeated num_peer_stats times;
      */
#else
    A_UINT32 rsvd          : 16, //[31:16]
             num_peer_stats: 16; //[15:0]
#endif
} xpan_peer_stats_fixed_param;

typedef struct {
    xpan_msg_hdr msg_hdr;
    xpan_peer_stats_fixed_param peer_stats_fixed_param;
} xpan_peer_stats_indication_msg;

typedef enum {
    XPAN_P2P,
    XPAN_WHC,
} XPAN_LINK_MODE;

typedef struct {
    A_UINT32 tlv_hdr; /* TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_mode_fixed_param */
    A_UINT32 link_mode; /* refer to XPAN_LINK_MODE enum */
} xpan_mode;

typedef struct {
    A_UINT32 tlv_hdr; /* TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_set_burst_interval_fixed_param */
    A_UINT32 burst_interval_ms;
} xpan_set_burst_interval_info;

typedef enum {
    XPAN_UDP_DATA,
    XPAN_UDP_TIME_SYNC,
    XPAN_ETHERTYPE,
    XPAN_UDP_HEARTBEAT,
} XPAN_RX_FILTER_TYPE;

typedef struct {
    A_UINT32 tlv_hdr; /* TLV tag and len; tag = LPASSCETLV_TAG_STRUC_xpan_set_rx_cce_filter_fixed_param */
#ifndef WIFI_BIT_ORDER_BIG_ENDIAN
    A_UINT32 filter_type: 2,  /* refer to XPAN_RX_FILTER_TYPE*/
             filter_value: 16,  /* Rx filter value=>can be UDP data port/UDP time sync port/ethertype */
             enable     : 1,  /* To set/clear CCE filter */
             rsvd       : 13;
#else
    A_UINT32 rsvd       : 13,
             enable     : 1,  /* To set/clear CCE filter */
             filter_value: 16,
             filter_type: 2,
#endif
} xpan_set_rx_filter_info;

enum xpan_wlan_fw_state {
    /**
     * Initial FW state when no use case is running and fresh state.
     * This should be set even when CE DL received by FW.
     */
    XPAN_WLAN_FW_DEFAULT_STATE = 0,

    /** When FW received ADSP crash indication */
    XPAN_WLAN_FW_RECVD_ADSP_CRASH_STATE = 1,

    /** after FW does the graceful cleanup */
    XPAN_WLAN_FW_ADSP_SSR_CLEANUP_DONE_STATE = 2,

    /** FW crashed state due to XPAN activities **/
    XPAN_WLAN_FW_CRASH_STATE_XPAN_REASON = 3,

    /** FW crashed state due to NON-XPAN activities **/
    XPAN_WLAN_FW_CRASH_STATE_DEFAULT_REASON = 4,
};

enum xpan_adsp_wifi_drv_state {
    /* Init state of adsp, when not even connected but lpass_init is called */
    XPAN_ADSP_WIFI_DRV_DEFAULT_STATE = 0,

    /* XPAN connected and in running case */
    XPAN_ADSP_WIFI_DRV_ACTIVE_STATE  = 1,

    /* ADSP Crash received via QMI */
    XPAN_ADSP_WIFI_DRV_CRASHED       = 2,
};

#endif /*_LPASS_CE_H_*/
