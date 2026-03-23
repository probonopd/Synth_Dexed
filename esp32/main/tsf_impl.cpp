/*
 * TinySoundFont compilation unit for ESP32.
 *
 * stb_vorbis and TSF are both compiled here, the only place.
 * All memory allocations (stb_vorbis decode workspace + TSF sample data)
 * are routed to PSRAM to keep the ~400 KB internal SRAM free.
 *
 * Diagnostic counters (readable via tsf_impl_get_*) are updated whenever
 * an allocation fails or stb_vorbis returns an error, so tsf_engine.cpp
 * can map the root cause to a specific LED error code.
 *
 * Error reasons reported by tsf_impl_get_failure_reason():
 *   0 = no failure yet
 *   1 = PSRAM exhausted during TSF_MALLOC/TSF_REALLOC
 *   2 = stb_vorbis decode error (bad data or corrupt OGG stream)
 *   3 = stb_vorbis alloc buffer too small (OOM inside the bump allocator)
 */

#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TIMPL_TAG = "tsf_impl";

/* ---- Diagnostic counters ---- */
static int s_tsf_alloc_total      = 0;  /* total TSF_MALLOC calls */
static int s_tsf_alloc_fail_count = 0;  /* failed TSF_MALLOC calls */
static int s_tsf_alloc_fallback   = 0;  /* fell back to internal SRAM */
static int s_last_vorbis_error    = 0;  /* last stb_vorbis error code */
static int s_vorbis_open_count    = 0;  /* number of stb_vorbis opens */
static int s_vorbis_fail_count    = 0;  /* number of failed opens */
static int s_failure_reason       = 0;  /* 0=ok, 1=psram, 2=vorbis-decode, 3=vorbis-oom */

/* ---- PSRAM allocator helpers ---- */
static void *tsf_esp32_malloc(size_t n)
{
    s_tsf_alloc_total++;
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!p) {
        /* Try internal SRAM as last resort */
        p = heap_caps_malloc(n, MALLOC_CAP_DEFAULT);
        if (p) {
            s_tsf_alloc_fallback++;
            ESP_LOGW(TIMPL_TAG,
                     "[tsf_malloc] PSRAM failed, fallback to internal SRAM: %u bytes"
                     "  (fallbacks=%d, PSRAM_free=%lu)",
                     (unsigned)n, s_tsf_alloc_fallback,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        } else {
            s_tsf_alloc_fail_count++;
            if (!s_failure_reason) s_failure_reason = 1;  /* PSRAM/heap exhaustion */
            ESP_LOGE(TIMPL_TAG,
                     "[tsf_malloc] ALLOC FAILED: %u bytes"
                     "  (total=%d fails=%d PSRAM_free=%lu internal_free=%lu)",
                     (unsigned)n, s_tsf_alloc_total, s_tsf_alloc_fail_count,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
    } else {
        ESP_LOGV(TIMPL_TAG,
                 "[tsf_malloc] %u bytes -> %p  (PSRAM_free=%lu)",
                 (unsigned)n, p,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    return p;
}

static void *tsf_esp32_realloc(void *ptr, size_t n)
{
    void *p = heap_caps_realloc(ptr, n, MALLOC_CAP_SPIRAM);
    if (!p) {
        p = heap_caps_realloc(ptr, n, MALLOC_CAP_DEFAULT);
        if (p) {
            s_tsf_alloc_fallback++;
            ESP_LOGW(TIMPL_TAG,
                     "[tsf_realloc] PSRAM realloc failed, fallback: %u bytes"
                     "  (PSRAM_free=%lu)",
                     (unsigned)n,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        } else {
            s_tsf_alloc_fail_count++;
            if (!s_failure_reason) s_failure_reason = 1;
            ESP_LOGE(TIMPL_TAG,
                     "[tsf_realloc] REALLOC FAILED: %u bytes"
                     "  (fails=%d PSRAM_free=%lu internal_free=%lu)",
                     (unsigned)n, s_tsf_alloc_fail_count,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        }
    }
    return p;
}

/* ---- stb_vorbis: override malloc/free so decode workspace goes to PSRAM ---- */
/* stb_vorbis uses plain malloc() for its internal codec state (~200 KB).       */
/* Without this override those allocations hit internal SRAM and cause crashes.  */
#define malloc(n)    tsf_esp32_malloc(n)
#define realloc(p,n) tsf_esp32_realloc(p, n)
#define free(p)      heap_caps_free(p)

extern "C" {
#define STB_VORBIS_NO_STDIO
#include "stb_vorbis.c"
}

#undef malloc
#undef realloc
#undef free

/* ---- stb_vorbis alloca() workaround ----------------------------------------
 *
 * TSF calls stb_vorbis_open_memory(data, len, NULL, NULL) — note the NULL
 * stb_vorbis_alloc pointer.  stb_vorbis.c defines:
 *
 *   #define temp_alloc(f,size) \
 *       (f->alloc.alloc_buffer ? setup_temp_malloc(f,size) : alloca(size))
 *
 * With a NULL alloc_buffer it falls back to alloca() for every per-frame
 * decode buffer.  On a Vorbis blocksize-1 frame that is up to
 *   blocksize * sizeof(float) = 8192 * 4 = 32 KB per alloca call,
 * which immediately overflows the 8 KB tsf_load_task stack.
 *
 * Fix: save the real stb_vorbis_open_memory function pointer before
 * overriding it with a macro.  The wrapper injects a 2 MB PSRAM-backed
 * stb_vorbis_alloc so stb_vorbis uses its bump-allocator path (no alloca).
 * The macro redirect is only visible inside the TSF_IMPLEMENTATION block
 * that follows, so there is no risk of recursive expansion.
 *
 * Buffer size rationale (2 MB):
 *   - stb_vorbis internal state:          ~80 KB
 *   - Per-channel temp buffers (max bl):  8192 * 4 * 2  = 64 KB
 *   - Floor/LSP/residue scratch:          ~100 KB
 *   - Codebook float arrays:              variable, up to ~200 KB per stream
 *   - Safety margin for complex SF3:      remaining
 *   Total comfortable budget: ~500 KB per stream; 2 MB gives 4x headroom.
 * -------------------------------------------------------------------------- */

typedef stb_vorbis *(*tsf_stb_open_fn_t)(const unsigned char *,
                                          int, int *,
                                          const stb_vorbis_alloc *);

/* Capture the real function address *before* the macro below shadows the name. */
static tsf_stb_open_fn_t s_real_stb_open = stb_vorbis_open_memory;

static uint8_t *s_vorbis_alloc_buf = nullptr;
static const int kVorbisAllocSize  = 256 * 1024;  /* 256 KB — measured: drums.sfo needs ~200 KB
                                                    * (codec state ~80 KB, codebooks ~120 KB,
                                                    * per-frame temp ~16 KB; 256 KB = safe margin)
                                                    * Reduced from 384 KB to save 128 KB PSRAM. */

static stb_vorbis *tsf_stb_open_with_psram(const unsigned char *data, int len,
                                            int *error_out,
                                            const stb_vorbis_alloc * /*hint*/)
{
    s_vorbis_open_count++;

    if (!s_vorbis_alloc_buf) {
        const unsigned long psram_before = (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        s_vorbis_alloc_buf = (uint8_t *)heap_caps_malloc(kVorbisAllocSize, MALLOC_CAP_SPIRAM);
        if (!s_vorbis_alloc_buf) {
            s_vorbis_alloc_buf = (uint8_t *)heap_caps_malloc(kVorbisAllocSize, MALLOC_CAP_DEFAULT);
            if (s_vorbis_alloc_buf) {
                ESP_LOGW(TIMPL_TAG,
                         "[vorbis] alloc_buf: PSRAM exhausted, using internal SRAM (%d bytes)"
                         "  PSRAM_free=%lu",
                         kVorbisAllocSize, psram_before);
            }
        }
        if (!s_vorbis_alloc_buf) {
            ESP_LOGE(TIMPL_TAG,
                     "[vorbis] FAILED to allocate %d-byte decode workspace!"
                     "  PSRAM_free=%lu internal_free=%lu",
                     kVorbisAllocSize,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        } else {
            ESP_LOGI(TIMPL_TAG,
                     "[vorbis] alloc_buf allocated: %d bytes @ %p  PSRAM_free=%lu",
                     kVorbisAllocSize, (void *)s_vorbis_alloc_buf,
                     (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }

    int err = 0;
    stb_vorbis *v = nullptr;

    if (s_vorbis_alloc_buf) {
        stb_vorbis_alloc a;
        a.alloc_buffer                 = (char *)s_vorbis_alloc_buf;
        a.alloc_buffer_length_in_bytes = kVorbisAllocSize;
        v = s_real_stb_open(data, len, &err, &a);
    } else {
        /* No workspace buffer — use alloca-based path (likely to crash on large
         * frames, but try anyway; at least log the failure). */
        ESP_LOGE(TIMPL_TAG,
                 "[vorbis] No alloc_buf — falling back to alloca path (may crash)");
        stb_vorbis_alloc a_null = {};
        v = s_real_stb_open(data, len, &err, &a_null);
    }

    if (error_out) *error_out = err;

    /* Log first bytes of the OGG data for diagnosis */
    if (!v || err != 0) {
        if (len >= 8) {
            ESP_LOGE(TIMPL_TAG,
                     "[vorbis] OGG data first 8 bytes: %02x %02x %02x %02x  %02x %02x %02x %02x",
                     data[0], data[1], data[2], data[3],
                     data[4], data[5], data[6], data[7]);
            if (data[0] != 'O' || data[1] != 'g' || data[2] != 'g' || data[3] != 'S') {
                ESP_LOGE(TIMPL_TAG, "[vorbis] Does NOT start with OggS capture pattern — data corrupt!");
            }
        }
    }

    if (!v || err != 0) {
        s_vorbis_fail_count++;
        s_last_vorbis_error = err;

        /* stb_vorbis error enum values (from stb_vorbis.h):
         *   0  = no_error    1  = need_more_data   2  = invalid_api_mixing
         *   3  = outofmem    4  = feature_not_supported  5 = too_many_channels
         *   6  = file_open_failure   7 = seek_without_length
         *   10 = unexpected_eof     11 = seek_invalid
         *   20 = invalid_setup      21 = invalid_stream
         *   30 = missing_capture_pattern  31..38 = various fatal stream errors   */
        const char *err_str = "unknown";
        switch (err) {
            case  0: err_str = "no_error(but v=NULL?)";      break;
            case  1: err_str = "VORBIS_need_more_data";      break;
            case  2: err_str = "VORBIS_invalid_api_mixing";  break;
            case  3: err_str = "VORBIS_outofmem";            break;
            case  4: err_str = "VORBIS_feature_not_supported"; break;
            case  5: err_str = "VORBIS_too_many_channels";   break;
            case  6: err_str = "VORBIS_file_open_failure";   break;
            case  7: err_str = "VORBIS_seek_without_length"; break;
            case 10: err_str = "VORBIS_unexpected_eof";      break;
            case 11: err_str = "VORBIS_seek_invalid";        break;
            case 20: err_str = "VORBIS_invalid_setup";       break;
            case 21: err_str = "VORBIS_invalid_stream";      break;
            case 30: err_str = "VORBIS_missing_capture_pattern"; break;
            case 31: err_str = "VORBIS_invalid_stream_structure_version"; break;
            case 32: err_str = "VORBIS_continued_packet_flag_invalid"; break;
            case 33: err_str = "VORBIS_incorrect_stream_serial_number"; break;
            case 34: err_str = "VORBIS_invalid_first_page";  break;
            case 35: err_str = "VORBIS_bad_packet_type";     break;
            case 36: err_str = "VORBIS_cant_find_last_page"; break;
            case 37: err_str = "VORBIS_seek_failed";         break;
            case 38: err_str = "VORBIS_ogg_skeleton_not_supported"; break;
        }

        if (!s_failure_reason) {
            s_failure_reason = (err == 3) ? 3 /* vorbis OOM */ : 2 /* vorbis decode error */;
        }

        ESP_LOGE(TIMPL_TAG,
                 "[vorbis] stb_vorbis_open_memory FAILED:"
                 "  error=%d (%s)  open#%d  len=%d bytes"
                 "  PSRAM_free=%lu  fails=%d",
                 err, err_str, s_vorbis_open_count, len,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 s_vorbis_fail_count);

        if (err == 3) {
            /* Out of memory inside vorbis bump-allocator */
            ESP_LOGE(TIMPL_TAG,
                     "[vorbis] OOM in bump-allocator — workspace was %d bytes."
                     " Increase kVorbisAllocSize if PSRAM allows.",
                     kVorbisAllocSize);
        }
    } else {
        stb_vorbis_info info = stb_vorbis_get_info(v);
        ESP_LOGI(TIMPL_TAG,
                 "[vorbis] open #%d OK: channels=%d sample_rate=%d"
                 "  max_frame=%d  setup_mem=%d  temp_mem=%d  len=%d bytes  PSRAM_free=%lu",
                 s_vorbis_open_count, info.channels, info.sample_rate,
                 info.max_frame_size, info.setup_memory_required,
                 info.temp_memory_required, len,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }

    return v;
}

/* Shadow the name so tsf.h (TSF_IMPLEMENTATION) calls our wrapper. */
#define stb_vorbis_open_memory tsf_stb_open_with_psram

/* Called by tsf_engine.cpp after tsf_load_memory() completes to reclaim the
 * 2 MB decode workspace.  stb_vorbis_close() is a no-op when alloc_buffer was
 * used, so the buffer is safe to free once the entire tsf_load_memory() call
 * (which opens, decodes, and closes the vorbis stream internally) returns. */
extern "C" void tsf_free_vorbis_buf(void)
{
    if (s_vorbis_alloc_buf) {
        ESP_LOGI(TIMPL_TAG,
                 "[vorbis] Freeing %d-byte decode workspace  PSRAM_free_before=%lu",
                 kVorbisAllocSize,
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        heap_caps_free(s_vorbis_alloc_buf);
        s_vorbis_alloc_buf = nullptr;
        ESP_LOGI(TIMPL_TAG,
                 "[vorbis] Workspace freed  PSRAM_free_after=%lu",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

/* ---- Diagnostic getter functions ---- */

extern "C" int tsf_impl_get_failure_reason(void)
{
    return s_failure_reason;
}

extern "C" int tsf_impl_get_last_vorbis_error(void)
{
    return s_last_vorbis_error;
}

extern "C" int tsf_impl_get_alloc_fail_count(void)
{
    return s_tsf_alloc_fail_count;
}

extern "C" void tsf_impl_dump_stats(void)
{
    ESP_LOGI(TIMPL_TAG,
             "[tsf_impl stats]"
             "  alloc_total=%d  alloc_fails=%d  alloc_fallbacks=%d"
             "  vorbis_opens=%d  vorbis_fails=%d  last_vorbis_err=%d"
             "  failure_reason=%d"
             "  PSRAM_free=%lu  internal_free=%lu",
             s_tsf_alloc_total, s_tsf_alloc_fail_count, s_tsf_alloc_fallback,
             s_vorbis_open_count, s_vorbis_fail_count, s_last_vorbis_error,
             s_failure_reason,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

extern "C" void tsf_impl_reset_counters(void)
{
    s_tsf_alloc_total      = 0;
    s_tsf_alloc_fail_count = 0;
    s_tsf_alloc_fallback   = 0;
    s_last_vorbis_error    = 0;
    s_vorbis_open_count    = 0;
    s_vorbis_fail_count    = 0;
    s_failure_reason       = 0;
}

/* ---- TSF: override allocator so decoded sample arrays go to PSRAM ---- */
#define TSF_MALLOC(size)        tsf_esp32_malloc(size)
#define TSF_REALLOC(ptr, size)  tsf_esp32_realloc(ptr, size)
#define TSF_FREE(ptr)           heap_caps_free(ptr)

/* ---- TSF implementation ----
 * TSF_NO_STDIO is intentionally NOT defined here so that tsf.h exposes
 * tsf_load(FILE*), which we use in tsf_engine.cpp to stream the SFO file
 * directly from SPIFFS without a 409 KB PSRAM pre-allocation buffer.
 * (STB_VORBIS_NO_STDIO is already defined above for stb_vorbis, which only
 *  ever uses stb_vorbis_open_memory — not file I/O.) */
#define TSF_IMPLEMENTATION
#include "tsf.h"
