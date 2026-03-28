/*
 * FMRack ESP32-S3 Port – USB Audio Class (UAC 1.0) output driver
 *
 * Registers a second USB host client that detects UAC 1.0 playback
 * devices (USB DACs, headphones, sound cards).  When one is found:
 *
 *   1. AudioControl and AudioStreaming interfaces are claimed.
 *   2. SET_INTERFACE selects the streaming alternate setting.
 *   3. Double-buffered isochronous OUT transfers deliver audio continuously.
 *
 * Audio data flow:
 *
 *   audio_write_task (Core 1)
 *     └─ esp32_usb_audio_write_stereo()  [lock-free ring push]
 *                │
 *   s_ring[] (internal SRAM, ~46 ms)
 *                │
 *   usb_audio_task (Core 0)  ← drains ring via isochronous OUT transfers
 *
 * The I2S output continues running in parallel; this driver adds the USB
 * device as a second (simultaneously active) audio sink.
 *
 * Channel mixing:
 *   1 ch  → (L+R)/2  (mono downmix)
 *   2 ch  → L, R     (stereo passthrough)
 *   3-7 ch → L, R, 0, 0, …  (stereo + silent surround channels)
 *
 * Sample rate conversion:
 *   Exact match     → passthrough
 *   44100 ↔ 48000   → linear interpolation
 *   Other mismatch  → device is skipped (incompatible)
 */

#include "esp32_usb_audio.h"
#include "esp32_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include "usb/usb_host.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "usb_audio";

/* ---------------------------------------------------------------------- */
/* USB Audio Class 1.0 constants                                           */
/* ---------------------------------------------------------------------- */
#define USB_CLASS_AUDIO                 0x01
#define USB_SUBCLASS_AUDIOCONTROL       0x01
#define USB_SUBCLASS_AUDIOSTREAMING     0x02

/* Class-specific interface descriptor sub-types (UAC 1.0 §A.5) */
#define UAC_SUBTYPE_FORMAT_TYPE         0x02
#define FORMAT_TYPE_I                   0x01

/* SET_INTERFACE setup packet fields */
#define SETUP_bmReqType_SET_INTF        0x01  /* host→device, standard, interface */
#define SETUP_bRequest_SET_INTERFACE    0x0B

/* ---------------------------------------------------------------------- */
/* Transfer geometry                                                        */
/* ---------------------------------------------------------------------- */
/* Isochronous packets per transfer — each packet = 1 USB full-speed frame
 * = 1 ms.  6 packets = 6 ms per transfer; gives the task loop ample time
 * to refill while the other buffer is in-flight. */
#define ISOC_PKTS_PER_XFER      6

/* Number of concurrent transfers (triple buffering).
 * 3 transfers × 6 ms each = 18 ms in the isochronous pipeline.
 * Double-buffering (2) left only 12 ms margin; any scheduling hiccup
 * caused a gap → audible choppiness.  Triple-buffering absorbs it. */
#define ISOC_XFER_COUNT         3

/* Maximum bytes per isochronous packet.
 * USB full-speed audio MPS is at most 1023 bytes.
 * 7 channels × 48000 Hz × 1 ms × 3 bytes = 1008 bytes — within limit. */
#define ISOC_PKT_MAX_BYTES      1023

/* Total data buffer per transfer. */
#define ISOC_XFER_BUF_BYTES     (ISOC_PKTS_PER_XFER * ISOC_PKT_MAX_BYTES)

/* Ring buffer: 4096 stereo pairs ≈ 85 ms at 48000 Hz.  Power-of-2 size
 * enables efficient lock-free SPSC (single producer / single consumer)
 * operation without cross-core spinlocks.  Kept in internal SRAM so ring
 * reads/writes never add PSRAM bus latency. */
#define RING_STEREO_PAIRS       4096
#define RING_SIZE_MASK          (RING_STEREO_PAIRS - 1)

/* ---------------------------------------------------------------------- */
/* Ring buffer (lock-free SPSC, internal SRAM)                             */
/*                                                                         */
/* Producer: audio_write_task on Core 1 (ring_push).                       */
/* Consumer: usb_audio_task / isoc callback on Core 0 (ring_pop).          */
/*                                                                         */
/* The old spinlock-based ring used portENTER_CRITICAL which disables      */
/* interrupts on BOTH cores for the duration of the per-sample copy loop.  */
/* With 256-pair pushes that's long enough to disturb USB SOF timing and   */
/* cause isochronous transfer gaps — audible as choppy playback.           */
/*                                                                         */
/* Lock-free SPSC with bulk memcpy eliminates cross-core contention        */
/* entirely and runs an order of magnitude faster.                         */
/* ---------------------------------------------------------------------- */
static DRAM_ATTR int16_t s_ring[RING_STEREO_PAIRS * 2]; /* interleaved L/R */
static volatile int s_ring_wr  = 0;  /* write head (in stereo pairs) */
static volatile int s_ring_rd  = 0;  /* read head  (in stereo pairs) */

/* Diagnostic counters (used by ring ops and heartbeat). */
static volatile uint32_t s_ring_underruns = 0;  /* pop got less than requested */
static volatile uint32_t s_ring_overflows = 0;  /* push had to drop data */

/* Push stereo pairs into ring.  Drops excess if ring is full (should not
 * happen in steady state since producer and consumer run at matched rates). */
static void ring_push(const int16_t *stereo, int pairs)
{
    int wr = s_ring_wr;
    int rd = __atomic_load_n(&s_ring_rd, __ATOMIC_ACQUIRE);
    int avail = (rd - wr - 1 + RING_STEREO_PAIRS) & RING_SIZE_MASK;
    if (pairs > avail) {
        s_ring_overflows++;
        pairs = avail;
    }
    if (pairs <= 0) return;

    /* Copy in up to two segments (handles wrap-around). */
    int first = RING_STEREO_PAIRS - wr;
    if (first > pairs) first = pairs;
    memcpy(&s_ring[wr * 2], stereo, first * 2 * sizeof(int16_t));
    int second = pairs - first;
    if (second > 0)
        memcpy(&s_ring[0], stereo + first * 2, second * 2 * sizeof(int16_t));

    __atomic_store_n(&s_ring_wr, (wr + pairs) & RING_SIZE_MASK, __ATOMIC_RELEASE);
}

/* Current fill level (approximate — no lock). */
static inline int ring_fill_level(void)
{
    int wr = __atomic_load_n(&s_ring_wr, __ATOMIC_ACQUIRE);
    int rd = __atomic_load_n(&s_ring_rd, __ATOMIC_ACQUIRE);
    return (wr - rd + RING_STEREO_PAIRS) & RING_SIZE_MASK;
}

/* Pop up to @p pairs stereo pairs from ring into @p out.
 * Returns number actually popped (may be less if ring is thin). */
static int ring_pop(int16_t *out, int pairs)
{
    int rd = s_ring_rd;
    int wr = __atomic_load_n(&s_ring_wr, __ATOMIC_ACQUIRE);
    int avail = (wr - rd + RING_STEREO_PAIRS) & RING_SIZE_MASK;
    if (pairs > avail) pairs = avail;
    if (pairs <= 0) return 0;

    /* Copy out in up to two segments (handles wrap-around). */
    int first = RING_STEREO_PAIRS - rd;
    if (first > pairs) first = pairs;
    memcpy(out, &s_ring[rd * 2], first * 2 * sizeof(int16_t));
    int second = pairs - first;
    if (second > 0)
        memcpy(out + first * 2, &s_ring[0], second * 2 * sizeof(int16_t));

    __atomic_store_n(&s_ring_rd, (rd + pairs) & RING_SIZE_MASK, __ATOMIC_RELEASE);
    return pairs;
}

/* ---------------------------------------------------------------------- */
/* Device state                                                            */
/* ---------------------------------------------------------------------- */
typedef enum {
    AUDIO_IDLE = 0,
    AUDIO_OPEN,        /* device open, descriptors parsed */
    AUDIO_SET_INTF,    /* awaiting SET_INTERFACE control response */
    AUDIO_STREAMING,   /* isochronous OUT active */
} audio_state_t;

typedef struct {
    usb_device_handle_t  dev_hdl;
    uint8_t  ctrl_intf;          /* AudioControl interface number */
    uint8_t  stream_intf;        /* AudioStreaming interface number */
    uint8_t  stream_alt;         /* Alternate setting that enables streaming */
    uint8_t  ep_out;             /* Isochronous OUT endpoint address */
    uint16_t ep_out_mps;         /* Max packet size */
    uint8_t  channels;           /* Output channel count (1-7) */
    uint8_t  bytes_per_sample;   /* 2 = 16-bit, 3 = 24-bit */
    uint32_t dev_sample_rate;    /* Device native sample rate (Hz) */

    /* Per-packet fractional sample tracking (avoids drift at 44100 Hz).
     * samples_per_ms = dev_sample_rate / 1000; fractional part × 1000 stored.
     * Each ms: frac_acc += frac_inc; if >= 1000, emit +1 sample and subtract 1000. */
    uint32_t base_samp_per_ms;   /* floor(dev_sample_rate / 1000) */
    uint32_t frac_inc;           /* frac_acc increment per ms */
    uint32_t frac_acc;           /* Current accumulator */

    /* SRC: fixed-point step for reading the ring when rates differ.
     * input_step_fp = (FMRACK_SAMPLE_RATE << 16) / dev_sample_rate
     * src_pos_fp advances through ring samples. */
    bool     need_src;
    uint32_t src_step_fp;        /* 16.16 fixed-point step */
    uint32_t src_pos_fp;         /* 16.16 position within current pop batch */

    usb_transfer_t *isoc_xfers[ISOC_XFER_COUNT];
    usb_transfer_t *ctrl_xfer;
    int      xfer_pending;       /* number of in-flight isoc transfers */
    audio_state_t state;
    bool     ctrl_intf_claimed;
    bool     stream_intf_claimed;
} usb_audio_dev_t;

static usb_audio_dev_t s_dev = {};
static usb_host_client_handle_t s_client = NULL;
static volatile bool s_running = false;
static TaskHandle_t  s_task    = NULL;
static volatile uint32_t s_isoc_ok_count = 0; /* successful isoc OUT completions */
static volatile uint32_t s_isoc_err_count_total = 0;
static volatile uint32_t s_resubmit_fails = 0;

/* Action flags set from client event callback */
#define ACTION_OPEN   (1u << 0)
#define ACTION_CLOSE  (1u << 1)
static volatile uint32_t s_actions   = 0;
static volatile uint8_t  s_new_addr  = 0;

/* SET_INTERFACE result (set in control transfer callback) */
static volatile bool s_set_intf_done = false;
static volatile bool s_set_intf_ok   = false;

/* ---------------------------------------------------------------------- */
/* SRC helpers                                                             */
/* ---------------------------------------------------------------------- */

/* Compute SRC step for reading from the ring (source = FMRACK_SAMPLE_RATE)
 * when the device runs at dev_rate.
 *
 *   src_pos advances: for each output device sample, step forward by
 *   FMRACK_SAMPLE_RATE / dev_rate source samples.
 *   If src_pos < num_source_samples − 1 → interpolate;
 *   else repeat last sample.
 */
static void src_init(usb_audio_dev_t *dev)
{
    dev->need_src   = false;
    dev->src_step_fp = 0;
    dev->src_pos_fp  = 0;

    uint32_t src_rate = (uint32_t)FMRACK_SAMPLE_RATE;
    uint32_t dst_rate = dev->dev_sample_rate;
    if (src_rate == dst_rate) return;

    /* Only support 44100 ↔ 48000 conversions. */
    bool ok = (src_rate == 44100 && dst_rate == 48000) ||
              (src_rate == 48000 && dst_rate == 44100);
    if (!ok) return;   /* caller will have already rejected device */

    dev->need_src    = true;
    /* step_fp = (src_rate << 16) / dst_rate */
    dev->src_step_fp = ((uint64_t)src_rate << 16) / dst_rate;
}

/* Read @p dst_pairs output stereo pairs from the ring with SRC applied.
 * Silences output if ring is dry.
 *
 * Key: only CONSUME the source samples actually used; the +1 boundary
 * sample needed for linear interpolation is PEEKed but left in the ring so
 * it becomes the first sample of the next call.  The old code popped all
 * "need" samples including the boundary, wasting ~2 samples per call and
 * draining the ring ~4 % faster than it was filled — audible as choppy
 * playback on any device whose sample rate differs from FMRACK_SAMPLE_RATE. */
static void src_pop_stereo(usb_audio_dev_t *dev, int16_t *dst, int dst_pairs)
{
    /* Compute exactly how many source samples are consumed by this batch. */
    uint32_t end_pos = dev->src_pos_fp + (uint32_t)dev->src_step_fp * dst_pairs;
    int consumed = (int)(end_pos >> 16);
    int need = consumed + 1;  /* +1 for interpolation boundary (peeked, not consumed) */

    static DRAM_ATTR int16_t src_tmp[512 * 2]; /* 512 stereo pairs */
    if (need > 512) { need = 512; consumed = need - 1; }

    /* Peek 'need' samples from ring WITHOUT advancing the read pointer. */
    int rd = s_ring_rd;
    int wr = __atomic_load_n(&s_ring_wr, __ATOMIC_ACQUIRE);
    int avail = (wr - rd + RING_STEREO_PAIRS) & RING_SIZE_MASK;
    int got = (need <= avail) ? need : avail;

    if (got < need)
        s_ring_underruns++;

    /* Copy from ring in up to two segments (handles wrap-around). */
    {
        int first = RING_STEREO_PAIRS - rd;
        if (first > got) first = got;
        memcpy(src_tmp, &s_ring[rd * 2], first * 2 * sizeof(int16_t));
        int second = got - first;
        if (second > 0)
            memcpy(src_tmp + first * 2, &s_ring[0], second * 2 * sizeof(int16_t));
    }

    /* Pad with silence if ring ran dry. */
    for (int i = got * 2; i < need * 2; i++) src_tmp[i] = 0;

    /* Advance ring read pointer by consumed only (boundary sample stays). */
    int actual_consume = (consumed <= avail) ? consumed : avail;
    __atomic_store_n(&s_ring_rd, (rd + actual_consume) & RING_SIZE_MASK, __ATOMIC_RELEASE);

    /* Linear interpolation. */
    uint32_t pos = dev->src_pos_fp;
    for (int i = 0; i < dst_pairs; i++) {
        int   i0   = pos >> 16;
        int   i1   = i0 + 1;
        if (i1 >= need) i1 = need - 1;
        uint32_t frac = pos & 0xFFFF;
        int32_t  f    = (int32_t)frac;
        int32_t  inv  = 65536 - f;
        dst[i * 2]     = (int16_t)((src_tmp[i0*2]   * inv + src_tmp[i1*2]   * f) >> 16);
        dst[i * 2 + 1] = (int16_t)((src_tmp[i0*2+1] * inv + src_tmp[i1*2+1] * f) >> 16);
        pos += dev->src_step_fp;
    }
    /* Keep fractional remainder for next call. */
    dev->src_pos_fp = pos & 0xFFFF;
}

/* ---------------------------------------------------------------------- */
/* Isochronous packet pack                                                  */
/* ---------------------------------------------------------------------- */

/* Pack @p frames stereo pairs → @p buf with channel mapping.
 * Reads from ring or via SRC depending on dev->need_src.
 * Returns bytes written. */
static int pack_frames(usb_audio_dev_t *dev, uint8_t *buf, int frames)
{
    /* Temp stereo buffer — at most ISOC_PKTS_PER_XFER × 49 frames each ms. */
    static DRAM_ATTR int16_t stereo_tmp[64 * 2]; /* 64 stereo pairs = 128 int16 */
    if (frames > 64) frames = 64;

    if (dev->need_src) {
        src_pop_stereo(dev, stereo_tmp, frames);
    } else {
        int got = ring_pop(stereo_tmp, frames);
        if (got < frames) {
            s_ring_underruns++;
            /* Fill silence for missing samples. */
            for (int i = got * 2; i < frames * 2; i++) stereo_tmp[i] = 0;
        }
    }

    int ch  = dev->channels;
    int bps = dev->bytes_per_sample;  /* 2 = 16-bit, 3 = 24-bit */
    uint8_t *p = buf;
    for (int f = 0; f < frames; f++) {
        int16_t L = stereo_tmp[f * 2];
        int16_t R = stereo_tmp[f * 2 + 1];
        if (ch == 1) {
            /* Mono: average L+R. */
            int16_t m = (int16_t)(((int32_t)L + R) >> 1);
            if (bps == 3) {
                /* 24-bit LE: pad LSB with 0, then 16-bit value in upper 2 bytes. */
                p[0] = 0;
                p[1] = (uint8_t)(m & 0xFF);
                p[2] = (uint8_t)(m >> 8);
                p += 3;
            } else {
                p[0] = (uint8_t)(m & 0xFF);
                p[1] = (uint8_t)(m >> 8);
                p += 2;
            }
        } else {
            /* Channel 0 = L, 1 = R, 2+ = silence. */
            for (int c = 0; c < ch; c++) {
                int16_t s = (c == 0) ? L : (c == 1) ? R : 0;
                if (bps == 3) {
                    p[0] = 0;
                    p[1] = (uint8_t)(s & 0xFF);
                    p[2] = (uint8_t)(s >> 8);
                    p += 3;
                } else {
                    p[0] = (uint8_t)(s & 0xFF);
                    p[1] = (uint8_t)(s >> 8);
                    p += 2;
                }
            }
        }
    }
    return (int)(p - buf);
}

/* Fill all packets of an isochronous transfer from the ring.
 * Uses the per-packet fractional sample counter to track 44.1 kHz drift.
 * Clamps each packet to the endpoint MPS to avoid truncation by the USB
 * host controller (e.g. 6-channel 48 kHz: 49 frames × 12 = 588 > MPS 576). */
static void fill_isoc_xfer(usb_audio_dev_t *dev, usb_transfer_t *xfer)
{
    uint8_t *buf = xfer->data_buffer;
    int total = 0;
    uint32_t max_frames_per_pkt = dev->ep_out_mps / (dev->channels * dev->bytes_per_sample);
    for (int pkt = 0; pkt < ISOC_PKTS_PER_XFER; pkt++) {
        /* Compute frames for this 1-ms packet. */
        uint32_t frames = dev->base_samp_per_ms;
        dev->frac_acc += dev->frac_inc;
        if (dev->frac_acc >= 1000) {
            dev->frac_acc -= 1000;
            frames++;
        }
        /* Clamp to endpoint MPS — without this, the extra fractional frame
         * pushes the packet past MPS on multi-channel devices. */
        if (frames > max_frames_per_pkt)
            frames = max_frames_per_pkt;
        int bytes = pack_frames(dev, buf + total, (int)frames);
        xfer->isoc_packet_desc[pkt].num_bytes = bytes;
        total += bytes;
    }
    xfer->num_bytes = total;
}

/* ---------------------------------------------------------------------- */
/* Transfer callbacks                                                      */
/* ---------------------------------------------------------------------- */
static void isoc_out_cb(usb_transfer_t *xfer)
{
    usb_audio_dev_t *dev = (usb_audio_dev_t *)xfer->context;
    dev->xfer_pending--;

    if (xfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
        /* Device gone — don't resubmit. */
        return;
    }

    if (xfer->status != USB_TRANSFER_STATUS_COMPLETED) {
        s_isoc_err_count_total++;
        if (s_isoc_err_count_total <= 10 || (s_isoc_err_count_total % 200) == 0) {
            ESP_LOGW(TAG, "Isoc OUT status=%d (count=%lu)", xfer->status,
                     (unsigned long)s_isoc_err_count_total);
        }
    } else {
        s_isoc_ok_count++;
    }

    if (dev->state != AUDIO_STREAMING) return;

    /* Refill and immediately resubmit for continuous playback.
     * For isochronous OUT this is safe to do directly in the callback.
     * Each transfer covers 6 ms so we're not in a tight spin. */
    fill_isoc_xfer(dev, xfer);
    esp_err_t err = usb_host_transfer_submit(xfer);
    if (err == ESP_OK) {
        dev->xfer_pending++;
    } else {
        s_resubmit_fails++;
        ESP_LOGW(TAG, "Isoc OUT resubmit failed: %s (count=%lu)",
                 esp_err_to_name(err), (unsigned long)s_resubmit_fails);
    }
}

static void ctrl_xfer_cb(usb_transfer_t *xfer)
{
    s_set_intf_ok   = (xfer->status == USB_TRANSFER_STATUS_COMPLETED);
    s_set_intf_done = true;
}

/* ---------------------------------------------------------------------- */
/* Client event callback                                                    */
/* ---------------------------------------------------------------------- */
static void audio_client_event_cb(const usb_host_client_event_msg_t *msg, void *arg)
{
    switch (msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        if (s_dev.state == AUDIO_IDLE) {
            s_new_addr  = msg->new_dev.address;
            s_actions  |= ACTION_OPEN;
        }
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        /* Only close if the audio device we opened is the one that left.
         * DEV_GONE fires for ALL device removals (any client), so we must
         * ignore removals of other devices (e.g. the USB MIDI keyboard). */
        if (s_dev.dev_hdl != NULL &&
                msg->dev_gone.dev_hdl == s_dev.dev_hdl) {
            ESP_LOGI(TAG, "USB audio device gone");
            s_actions |= ACTION_CLOSE;
        }
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------- */
/* Descriptor walking                                                      */
/* ---------------------------------------------------------------------- */
typedef struct {
    uint8_t  stream_intf;
    uint8_t  stream_alt;
    uint8_t  ep_addr;
    uint16_t ep_mps;
    uint8_t  channels;
    uint8_t  bytes_per_sample;  /* 2 = 16-bit, 3 = 24-bit */
    uint32_t sample_rate;
    bool     found;
} audio_stream_info_t;

static bool find_audio_stream(usb_device_handle_t dev_hdl,
                               uint8_t *ctrl_intf_out,
                               audio_stream_info_t *info)
{
    const usb_config_desc_t *desc = NULL;
    if (usb_host_get_active_config_descriptor(dev_hdl, &desc) != ESP_OK || !desc)
        return false;

    uint16_t total = desc->wTotalLength;
    int ofs = 0;
    const usb_standard_desc_t *cur = (const usb_standard_desc_t *)desc;

    ESP_LOGI(TAG, "Config descriptor: wTotalLength=%d bNumInterfaces=%d",
             total, desc->bNumInterfaces);

    bool in_stream = false;
    uint8_t cur_audio_intf = 0;
    uint8_t cur_alt = 0;
    bool ctrl_found = false;
    /* Track format acceptance for the current alt setting */
    bool cur_alt_has_ep   = false;
    uint8_t  cur_alt_ep   = 0;
    uint16_t cur_alt_mps  = 0;
    bool cur_alt_fmt_ok   = false;
    uint8_t  cur_alt_ch   = 0;
    uint8_t  cur_alt_bps  = 0;  /* bytes per sample (2 or 3) */
    uint32_t cur_alt_rate = 0;

    while (cur) {
        uint8_t dtype = cur->bDescriptorType;
        uint8_t dlen  = cur->bLength;
        const uint8_t *d = (const uint8_t *)cur;

        if (dtype == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            /* Commit previous alt if it was a valid stream. */
            if (in_stream && cur_alt > 0 && cur_alt_has_ep && cur_alt_fmt_ok) {
                /* Check sample rate compatibility. */
                uint32_t src = (uint32_t)FMRACK_SAMPLE_RATE;
                bool rate_ok = (cur_alt_rate == src) ||
                               (src == 44100 && cur_alt_rate == 48000) ||
                               (src == 48000 && cur_alt_rate == 44100);
                ESP_LOGI(TAG, "  alt %d: ch=%d %dbit rate=%lu ep=0x%02X MPS=%d rate_ok=%d",
                         cur_alt, cur_alt_ch, cur_alt_bps * 8,
                         (unsigned long)cur_alt_rate,
                         cur_alt_ep, cur_alt_mps, (int)rate_ok);
                if (rate_ok && cur_alt_ch >= 1 && cur_alt_ch <= 7 && !info->found) {
                    info->stream_intf     = cur_audio_intf;
                    info->stream_alt      = cur_alt;
                    info->ep_addr         = cur_alt_ep;
                    info->ep_mps          = cur_alt_mps;
                    info->channels        = cur_alt_ch;
                    info->bytes_per_sample = cur_alt_bps;
                    info->sample_rate     = cur_alt_rate;
                    info->found           = true;
                    ESP_LOGI(TAG, "AudioStreaming intf=%d alt=%d ep=0x%02X ch=%d %dbit rate=%lu",
                             cur_audio_intf, cur_alt, cur_alt_ep,
                             cur_alt_ch, cur_alt_bps * 8, (unsigned long)cur_alt_rate);
                }
            }

            /* Parse this interface descriptor. */
            if (dlen >= 9) {
                uint8_t inum   = d[2];
                uint8_t ialt   = d[3];
                uint8_t iclass = d[5];
                uint8_t isub   = d[6];

                ESP_LOGI(TAG, "  intf %d alt %d class=0x%02X sub=0x%02X proto=0x%02X",
                         inum, ialt, iclass, isub, d[7]);

                in_stream = false;

                if (iclass == USB_CLASS_AUDIO) {
                    cur_audio_intf = inum;
                    cur_alt = ialt;
                    if (isub == USB_SUBCLASS_AUDIOCONTROL && ialt == 0) {
                        ctrl_found = true;
                        *ctrl_intf_out = inum;
                    } else if (isub == USB_SUBCLASS_AUDIOSTREAMING) {
                        in_stream = true;
                        if (ialt > 0) {
                            /* Reset per-alt-setting state. */
                            cur_alt_has_ep  = false;
                            cur_alt_ep      = 0;
                            cur_alt_mps     = 0;
                            cur_alt_fmt_ok  = false;
                            cur_alt_ch      = 0;
                            cur_alt_bps     = 0;
                            cur_alt_rate    = 0;
                        }
                    }
                }
            }
        } else if (in_stream && dtype == USB_B_DESCRIPTOR_TYPE_ENDPOINT && dlen >= 7) {
            /* Endpoint descriptor within an AudioStreaming alt setting. */
            uint8_t  ep_addr  = d[2];
            uint8_t  ep_type  = d[3] & 0x03; /* bmAttributes[1:0] */
            uint16_t ep_mps   = (uint16_t)(d[4] | (d[5] << 8)) & 0x07FF;
            bool     is_out   = !(ep_addr & 0x80);
            ESP_LOGI(TAG, "  EP 0x%02X type=%d MPS=%d %s",
                     ep_addr, ep_type, ep_mps, is_out ? "OUT" : "IN");
            if (ep_type == 0x01 /* isochronous */ && is_out) {
                cur_alt_has_ep = true;
                cur_alt_ep     = ep_addr;
                cur_alt_mps    = ep_mps;
            }
        } else if (in_stream && cur_alt > 0 &&
                   dtype == 0x24 /* CS_INTERFACE */ && dlen >= 8) {
            /* Class-specific AudioStreaming descriptor. */
            uint8_t sub = d[2];
            if (sub == UAC_SUBTYPE_FORMAT_TYPE && d[3] == FORMAT_TYPE_I) {
                /* TYPE_I FORMAT descriptor:
                 *  d[4] = bNrChannels
                 *  d[5] = bSubFrameSize (bytes / subframe)
                 *  d[6] = bBitResolution
                 *  d[7] = bSamFreqType (0=continuous, N=discrete)
                 *  d[8..]: sample frequencies (3 bytes each) */
                uint8_t nr_ch     = d[4];
                uint8_t sub_frame = d[5];
                uint8_t bit_res   = d[6];
                uint8_t freq_type = (dlen > 7) ? d[7] : 0;
                /* Accept 16-bit (2 bytes/sample) and 24-bit (3 bytes/sample). */
                bool fmt_ok = ((sub_frame == 2 && bit_res == 16) ||
                               (sub_frame == 3 && bit_res == 24)) &&
                              nr_ch >= 1 && nr_ch <= 7;
                if (!fmt_ok) {
                    ESP_LOGI(TAG, "  Skipping FORMAT_TYPE_I: ch=%d sub_frame=%d bit_res=%d",
                             nr_ch, sub_frame, bit_res);
                }
                if (fmt_ok) {
                    cur_alt_ch  = nr_ch;
                    cur_alt_bps = sub_frame;
                    /* Extract first sample frequency. */
                    if (freq_type == 0 && dlen >= 14) {
                        /* Continuous range — use lower bound. */
                        cur_alt_rate = (uint32_t)d[8] | ((uint32_t)d[9] << 8) | ((uint32_t)d[10] << 16);
                    } else if (freq_type >= 1 && dlen >= 8 + 3) {
                        for (int fi = 0; fi < freq_type && (8 + fi*3 + 2) < dlen; fi++) {
                            uint32_t r = (uint32_t)d[8+fi*3]
                                       | ((uint32_t)d[9+fi*3] << 8)
                                       | ((uint32_t)d[10+fi*3] << 16);
                            /* Prefer exact match; otherwise accept 44100↔48000 pair. */
                            uint32_t want = (uint32_t)FMRACK_SAMPLE_RATE;
                            if (r == want) { cur_alt_rate = r; break; }
                            if ((r == 44100 && want == 48000) ||
                                (r == 48000 && want == 44100)) cur_alt_rate = r;
                            if (fi == 0 && cur_alt_rate == 0) cur_alt_rate = r;
                        }
                    }
                    if (cur_alt_rate > 0) cur_alt_fmt_ok = true;
                }
            }
        }

        cur = usb_parse_next_descriptor(cur, total, &ofs);
    }

    /* Commit the last alt setting if it wasn't committed by a following interface. */
    if (in_stream && cur_alt > 0 && cur_alt_has_ep && cur_alt_fmt_ok) {
        uint32_t src = (uint32_t)FMRACK_SAMPLE_RATE;
        bool rate_ok = (cur_alt_rate == src) ||
                       (src == 44100 && cur_alt_rate == 48000) ||
                       (src == 48000 && cur_alt_rate == 44100);
        ESP_LOGI(TAG, "  alt %d (tail): ch=%d %dbit rate=%lu ep=0x%02X MPS=%d rate_ok=%d",
                 cur_alt, cur_alt_ch, cur_alt_bps * 8, (unsigned long)cur_alt_rate,
                 cur_alt_ep, cur_alt_mps, (int)rate_ok);
        if (rate_ok && cur_alt_ch >= 1 && cur_alt_ch <= 7 && !info->found) {
            info->stream_intf     = cur_audio_intf;
            info->stream_alt      = cur_alt;
            info->ep_addr         = cur_alt_ep;
            info->ep_mps          = cur_alt_mps;
            info->channels        = cur_alt_ch;
            info->bytes_per_sample = cur_alt_bps;
            info->sample_rate     = cur_alt_rate;
            info->found           = true;
        }
    }

    return ctrl_found && info->found;
}

/* ---------------------------------------------------------------------- */
/* Device open / close                                                     */
/* ---------------------------------------------------------------------- */
static void close_audio_device(void)
{
    if (!s_dev.dev_hdl) return;

    /* Stop any new resubmissions from the isoc callback first. */
    s_dev.state = AUDIO_IDLE;

    /* Releasing the streaming interface cancels all pending isochronous
     * transfers and drives the callbacks with CANCELED status before
     * returning.  Only after that is it safe to free the transfer objects. */
    if (s_dev.stream_intf_claimed) {
        usb_host_interface_release(s_client, s_dev.dev_hdl, s_dev.stream_intf);
        s_dev.stream_intf_claimed = false;
    }
    if (s_dev.ctrl_intf_claimed) {
        usb_host_interface_release(s_client, s_dev.dev_hdl, s_dev.ctrl_intf);
        s_dev.ctrl_intf_claimed = false;
    }

    /* Now safe to free transfer objects (all pending transfers have completed
     * or been cancelled by usb_host_interface_release above). */
    for (int i = 0; i < ISOC_XFER_COUNT; i++) {
        if (s_dev.isoc_xfers[i]) {
            usb_host_transfer_free(s_dev.isoc_xfers[i]);
            s_dev.isoc_xfers[i] = NULL;
        }
    }
    if (s_dev.ctrl_xfer) {
        usb_host_transfer_free(s_dev.ctrl_xfer);
        s_dev.ctrl_xfer = NULL;
    }

    usb_host_device_close(s_client, s_dev.dev_hdl);
    memset(&s_dev, 0, sizeof(s_dev));
    ESP_LOGI(TAG, "USB audio device closed");
}

static void open_audio_device(uint8_t addr)
{
    usb_device_handle_t hdl = NULL;
    if (usb_host_device_open(s_client, addr, &hdl) != ESP_OK) return;

    uint8_t ctrl_intf = 0;
    audio_stream_info_t info = {};
    if (!find_audio_stream(hdl, &ctrl_intf, &info)) {
        ESP_LOGI(TAG, "Device %d: no compatible AudioStreaming interface found", addr);
        usb_host_device_close(s_client, hdl);
        return;
    }

    s_dev.dev_hdl      = hdl;
    s_dev.ctrl_intf    = ctrl_intf;
    s_dev.stream_intf  = info.stream_intf;
    s_dev.stream_alt   = info.stream_alt;
    s_dev.ep_out       = info.ep_addr;
    s_dev.ep_out_mps   = info.ep_mps;
    s_dev.channels     = info.channels;
    s_dev.bytes_per_sample = info.bytes_per_sample;
    s_dev.dev_sample_rate = info.sample_rate;

    /* Fractional sample counter for 44.1 kHz etc. */
    s_dev.base_samp_per_ms = info.sample_rate / 1000;
    s_dev.frac_inc         = info.sample_rate % 1000;
    s_dev.frac_acc         = 0;

    src_init(&s_dev);

    ESP_LOGI(TAG, "USB audio: ch=%d %dbit rate=%lu Hz ep=0x%02X MPS=%d need_src=%d",
             s_dev.channels, s_dev.bytes_per_sample * 8,
             (unsigned long)s_dev.dev_sample_rate,
             s_dev.ep_out, s_dev.ep_out_mps, (int)s_dev.need_src);

    /* Verify and clamp packet size to fit endpoint MPS.
     * With multi-channel devices at high sample rates, (base_samp_per_ms + 1 fractional)
     * can exceed the endpoint MPS. Clamp max_frames to ensure packets fit.
     * Example: 6ch 48kHz 16-bit: 49 frames × 12 bytes = 588 > MPS 576 */
    uint32_t max_frames = s_dev.base_samp_per_ms + 1; /* +1 for fractional rounding */
    uint32_t bytes_per_pkt = max_frames * s_dev.channels * s_dev.bytes_per_sample;

    if (bytes_per_pkt > s_dev.ep_out_mps) {
        /* Clamp to fit within MPS */
        max_frames = s_dev.ep_out_mps / (s_dev.channels * s_dev.bytes_per_sample);
        bytes_per_pkt = max_frames * s_dev.channels * s_dev.bytes_per_sample;
        ESP_LOGW(TAG, "USB audio: packet size exceeded MPS, clamped to max_frames=%lu (bytes=%lu)",
                 (unsigned long)max_frames, (unsigned long)bytes_per_pkt);
    }

    ESP_LOGI(TAG, "USB audio: bytes/pkt=%lu (max_frames=%lu × ch=%d × %d), ep MPS=%d",
             (unsigned long)bytes_per_pkt, (unsigned long)max_frames,
             s_dev.channels, s_dev.bytes_per_sample, s_dev.ep_out_mps);

    /* Claim AudioControl interface. */
    if (usb_host_interface_claim(s_client, hdl, ctrl_intf, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to claim AudioControl intf %d", ctrl_intf);
        usb_host_device_close(s_client, hdl);
        memset(&s_dev, 0, sizeof(s_dev));
        return;
    }
    s_dev.ctrl_intf_claimed = true;

    /* Claim AudioStreaming interface with the active alt setting so that
     * ESP-IDF USB host creates a pipe for the isochronous OUT endpoint.
     * Claiming alt=0 (zero-bandwidth) would leave no pipe for EP OUT, and
     * subsequent usb_host_transfer_submit() calls would silently never
     * complete. */
    if (usb_host_interface_claim(s_client, hdl, info.stream_intf, info.stream_alt) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to claim AudioStreaming intf %d alt %d",
                 info.stream_intf, info.stream_alt);
        usb_host_interface_release(s_client, hdl, ctrl_intf);
        usb_host_device_close(s_client, hdl);
        memset(&s_dev, 0, sizeof(s_dev));
        return;
    }
    s_dev.stream_intf_claimed = true;

    /* Send SET_INTERFACE to activate the streaming alternate setting. */
    if (usb_host_transfer_alloc(8, 0, &s_dev.ctrl_xfer) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to alloc control transfer");
        close_audio_device();
        return;
    }
    uint8_t *sp = s_dev.ctrl_xfer->data_buffer;
    sp[0] = SETUP_bmReqType_SET_INTF;
    sp[1] = SETUP_bRequest_SET_INTERFACE;
    sp[2] = (uint8_t)(info.stream_alt & 0xFF);
    sp[3] = 0;
    sp[4] = (uint8_t)(info.stream_intf & 0xFF);
    sp[5] = 0;
    sp[6] = 0; /* wLength = 0 */
    sp[7] = 0;
    s_dev.ctrl_xfer->device_handle    = hdl;
    s_dev.ctrl_xfer->bEndpointAddress = 0;  /* control endpoint */
    s_dev.ctrl_xfer->callback         = ctrl_xfer_cb;
    s_dev.ctrl_xfer->context          = &s_dev;
    s_dev.ctrl_xfer->num_bytes        = 8;

    s_set_intf_done = false;
    s_set_intf_ok   = false;
    s_dev.state = AUDIO_SET_INTF;

    /* Control transfers (EP0) must use usb_host_transfer_submit_control(),
     * not the generic usb_host_transfer_submit() which is for bulk/isoc/intr. */
    esp_err_t ce = usb_host_transfer_submit_control(s_client, s_dev.ctrl_xfer);
    if (ce != ESP_OK) {
        ESP_LOGE(TAG, "Failed to submit SET_INTERFACE: %s", esp_err_to_name(ce));
        close_audio_device();
    }
}

/* Allocate isochronous transfers and start streaming. */
static void start_streaming(void)
{
    for (int i = 0; i < ISOC_XFER_COUNT; i++) {
        /* Allocate transfer with data buffer in internal RAM (DMA-capable). */
        usb_transfer_t *xfer = NULL;
        if (usb_host_transfer_alloc(ISOC_XFER_BUF_BYTES, ISOC_PKTS_PER_XFER, &xfer) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to alloc isoc transfer %d", i);
            close_audio_device();
            return;
        }

        /* usb_host_transfer_alloc already places the data buffer in
         * internal DMA-capable memory (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA).
         * Do NOT replace data_buffer — the USB host library manages its
         * lifetime and alignment requirements internally. */

        xfer->device_handle    = s_dev.dev_hdl;
        xfer->bEndpointAddress = s_dev.ep_out;
        xfer->callback         = isoc_out_cb;
        xfer->context          = &s_dev;

        s_dev.isoc_xfers[i] = xfer;
    }

    s_dev.state       = AUDIO_STREAMING;
    s_dev.xfer_pending = 0;

    /* Prime the ring buffer with silence so the first isochronous transfers
     * don't underrun before the audio write task starts feeding data.
     * Fill half the ring (~42 ms at 48 kHz) to give ample headroom for all
     * three initial transfer submissions plus early scheduling jitter. */
    {
        int16_t silence[64 * 2]; /* 64 stereo pairs × 4 bytes = 512 bytes on stack */
        memset(silence, 0, sizeof(silence));
        for (int i = 0; i < (RING_STEREO_PAIRS / 2) / 64; i++)
            ring_push(silence, 64);
    }

    /* Fill and submit all transfers. */
    for (int i = 0; i < ISOC_XFER_COUNT; i++) {
        fill_isoc_xfer(&s_dev, s_dev.isoc_xfers[i]);
        esp_err_t se = usb_host_transfer_submit(s_dev.isoc_xfers[i]);
        if (se == ESP_OK) {
            s_dev.xfer_pending++;
        } else {
            ESP_LOGE(TAG, "Initial isoc submit [%d] failed: %s", i, esp_err_to_name(se));
        }
    }

    ESP_LOGI(TAG, "USB audio streaming started (ch=%d rate=%lu Hz pkts/xfer=%d pending=%d)",
             s_dev.channels, (unsigned long)s_dev.dev_sample_rate,
             ISOC_PKTS_PER_XFER, s_dev.xfer_pending);
}

/* ---------------------------------------------------------------------- */
/* USB audio task                                                          */
/* ---------------------------------------------------------------------- */
static void usb_audio_task(void *arg)
{
    ESP_LOGI(TAG, "USB audio task started on core %d", xPortGetCoreID());

    usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = audio_client_event_cb,
            .callback_arg          = NULL,
        },
    };
    if (usb_host_client_register(&cfg, &s_client) != ESP_OK) {
        ESP_LOGE(TAG, "USB audio client register failed");
        vTaskDelete(NULL);
        return;
    }

    /* Check for already-enumerated devices at startup.
     * esp32_usb_audio_init() is called after the 2-second USB settle delay in
     * main.cpp so any device already on the bus has been enumerated by here. */
    {
        uint8_t addrs[8];
        int n = 0;
        if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK && n > 0) {
            ESP_LOGI(TAG, "Startup scan: %d device(s) already enumerated", n);
            for (int i = 0; i < n && s_dev.state == AUDIO_IDLE; i++)
                open_audio_device(addrs[i]);
        } else {
            ESP_LOGI(TAG, "Startup scan: no devices enumerated yet");
        }
    }

    while (s_running) {
        /* When streaming, 1 ms timeout keeps us responsive to isochronous
         * callbacks.  When idle (no device), back off to 50 ms to save CPU
         * — there is nothing time-critical to service. */
        const int timeout_ms = (s_dev.state == AUDIO_IDLE) ? 50 : 1;
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(timeout_ms));

        /* Process connect action. */
        if ((s_actions & ACTION_OPEN) && s_dev.state == AUDIO_IDLE) {
            s_actions &= ~ACTION_OPEN;
            open_audio_device(s_new_addr);
        }

        /* Process SET_INTERFACE completion. */
        if (s_dev.state == AUDIO_SET_INTF && s_set_intf_done) {
            s_set_intf_done = false;
            if (s_set_intf_ok) {
                start_streaming();
            } else {
                ESP_LOGE(TAG, "SET_INTERFACE failed — no audio output");
                close_audio_device();
            }
        }

        /* Process disconnect action. */
        if (s_actions & ACTION_CLOSE) {
            s_actions &= ~ACTION_CLOSE;
            close_audio_device();
        }

        /* Periodic re-scan for late-enumerating devices.
         * USB hubs sometimes fail CHECK_SHORT_DEV_DESC on first attempt;
         * the ESP-IDF USB host retries internally but by the time it
         * succeeds, our startup scan and NEW_DEV event may both have
         * been missed.  Re-scan every ~3 s when idle. */
        static uint32_t s_rescan_count = 0;
        if (s_dev.state == AUDIO_IDLE && ++s_rescan_count >= 3000) {
            s_rescan_count = 0;
            uint8_t addrs[8];
            int n = 0;
            if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &n) == ESP_OK && n > 0) {
                for (int i = 0; i < n && s_dev.state == AUDIO_IDLE; i++)
                    open_audio_device(addrs[i]);
            }
        }
        /* Reset rescan counter when not idle (device connected). */
        if (s_dev.state != AUDIO_IDLE) s_rescan_count = 0;

        /* Heartbeat — only log when state or counters change, avoiding
         * log spam when the system is idle or streaming steadily.
         * Check every ~2 s based on elapsed iterations (varies with timeout). */
        static uint32_t s_loop_count = 0;
        static int      s_last_state = -1;
        static uint32_t s_last_ok    = 0;
        static uint32_t s_last_err   = 0;
        static uint32_t s_last_under = 0;
        static uint32_t s_last_over  = 0;
        const uint32_t heartbeat_iters = (s_dev.state == AUDIO_IDLE) ? 40 : 2000;
        if (++s_loop_count >= heartbeat_iters) {
            s_loop_count = 0;
            int fill = ring_fill_level();
            bool changed = ((int)s_dev.state != s_last_state ||
                            s_isoc_ok_count != s_last_ok ||
                            s_isoc_err_count_total != s_last_err ||
                            s_ring_underruns != s_last_under ||
                            s_ring_overflows != s_last_over);
            if (changed) {
                ESP_LOGI(TAG, "USB-A: st=%d pend=%d ok=%lu err=%lu ring=%d/%d "
                         "underrun=%lu overflow=%lu resubfail=%lu",
                         (int)s_dev.state, s_dev.xfer_pending,
                         (unsigned long)s_isoc_ok_count,
                         (unsigned long)s_isoc_err_count_total,
                         fill, RING_STEREO_PAIRS - 1,
                         (unsigned long)s_ring_underruns,
                         (unsigned long)s_ring_overflows,
                         (unsigned long)s_resubmit_fails);
                s_last_state = (int)s_dev.state;
                s_last_ok    = s_isoc_ok_count;
                s_last_err   = s_isoc_err_count_total;
                s_last_under = s_ring_underruns;
                s_last_over  = s_ring_overflows;
            }
        }
    }

    close_audio_device();
    usb_host_client_deregister(s_client);
    s_client = NULL;
    ESP_LOGI(TAG, "USB audio task exiting");
    vTaskDelete(NULL);
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

int esp32_usb_audio_init(void)
{
    if (s_running) return 0;
    s_running = true;

    BaseType_t ok = xTaskCreatePinnedToCore(
        usb_audio_task, "usb_audio",
        6 * 1024, NULL,
        /* Same priority as USB MIDI — isochronous transfers are equally
         * time-critical and must not be starved by the MIDI task. */
        USB_MIDI_TASK_PRIORITY,
        &s_task,
        USB_MIDI_TASK_CORE);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB audio task");
        s_running = false;
        return -1;
    }
    ESP_LOGI(TAG, "USB audio driver initialised");
    return 0;
}

void esp32_usb_audio_stop(void)
{
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(200));
    s_task = NULL;
}

bool esp32_usb_audio_is_ready(void)
{
    return s_dev.state == AUDIO_STREAMING;
}

int esp32_usb_audio_get_channels(void)
{
    return (s_dev.state == AUDIO_STREAMING) ? (int)s_dev.channels : 0;
}

void esp32_usb_audio_write_stereo(const int16_t *buf, int num_stereo_pairs)
{
    if (s_dev.state != AUDIO_STREAMING) return;
    ring_push(buf, num_stereo_pairs);
}
