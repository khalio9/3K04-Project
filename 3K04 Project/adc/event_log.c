/* event_log.c - EventLog implementation
 *
 * Everything in this file that is `static` is the module's SECRET:
 *   - the file format (one text line per entry, fields separated by '|')
 *   - the hash chain (each line ends with hash(prev hash + this line))
 *   - durability (fflush + fsync before append returns; partial last line dropped at load)
 * No other module can see or change these, so they can be replaced
 * (e.g. a database, or SHA-256 instead of FNV) without touching any other file.
 */
#define __USE_MINGW_ANSI_STDIO 1   /* Windows: makes %lld work properly */

#define _POSIX_C_SOURCE 200809L
#include "event_log.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32                      /* Windows (MinGW) equivalents */
#include <io.h>
#define fsync(fd)          _commit(fd)
#define ftruncate(fd, n)   _chsize(fd, n)
static int truncate(const char *path, long n)
{
    FILE *f = fopen(path, "r+b");
    if (!f) return -1;
    int r = _chsize(_fileno(f), n);
    fclose(f);
    return r;
}
#define strncasecmp _strnicmp
#else
#include <strings.h>
#include <unistd.h>
#endif

/* ---------- private state ---------- */
static LogEntry *s_entries  = NULL;   /* all entries, in seq order */
static int       s_count    = 0;
static int       s_capacity = 0;
static unsigned  s_next_seq = 1;
static bool      s_healthy  = true;
static uint64_t  s_last_hash = 0;     /* hash of the newest valid entry (chain head) */
static char      s_adc_id[ADC_ID_MAX];
static char      s_path[512];
static NowMsFn   s_now      = NULL;
static FILE     *s_file     = NULL;

#ifdef ADC_SIM
static bool s_sim_fail_writes = false;   /* demo hook: pretend the disk failed */
void event_log_sim_fail_writes(bool fail) { s_sim_fail_writes = fail; }
#endif

/* ---------- hash chain (FNV-1a 64-bit; replaceable without affecting clients) ---------- */
static uint64_t fnv1a(uint64_t h, const char *s)
{
    if (h == 0) h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}

static uint64_t chain_hash(uint64_t prev, const char *content)
{
    char prevhex[17];
    snprintf(prevhex, sizeof prevhex, "%016" PRIx64, prev);
    return fnv1a(fnv1a(0, prevhex), content);
}

/* ---------- helpers ---------- */
static void copy_clean(char *dst, size_t cap, const char *src)
{
    /* '|' and newlines would break the file format, so they are replaced. */
    size_t i = 0;
    if (src) {
        for (; src[i] && i + 1 < cap; i++) {
            char c = src[i];
            dst[i] = (c == '|' || c == '\n' || c == '\r') ? '/' : c;
        }
    }
    dst[i] = '\0';
}

static bool key_is(const char *key, size_t len, const char *word)
{
    size_t n = strlen(word);
    if (len != n) return false;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)key[i]) != word[i]) return false;
    return true;
}

/* Row L2: a key that holds a credential secret, or patient data other than patient_id. */
static bool details_forbidden(const char *details)
{
    if (!details) return false;
    const char *p = details;
    while (*p) {
        while (*p == ';' || *p == ' ') p++;
        const char *key = p;
        while (*p && *p != '=' && *p != ';') p++;
        size_t len = (size_t)(p - key);
        if (len > 0) {
            if (key_is(key, len, "pin") || key_is(key, len, "password") ||
                key_is(key, len, "secret") || key_is(key, len, "credential"))
                return true;
            if (len >= 7 && strncasecmp(key, "patient", 7) == 0 &&
                !key_is(key, len, "patient_id"))
                return true;
        }
        while (*p && *p != ';') p++;
    }
    return false;
}

/* Text of one entry without its hash: seq|time|adc|type|user|outcome|details */
static void format_content(const LogEntry *e, char *buf, size_t cap)
{
    snprintf(buf, cap, "%u|%" PRId64 "|%s|%d|%s|%d|%s",
             e->seq, (int64_t)e->time_ms, e->adc_id, (int)e->event_type,
             e->user_id, (int)e->outcome, e->details);
}

static int push_entry(const LogEntry *e)
{
    if (s_count == s_capacity) {
        int cap = s_capacity ? s_capacity * 2 : 256;
        LogEntry *n = realloc(s_entries, (size_t)cap * sizeof *n);
        if (!n) return -1;
        s_entries = n;
        s_capacity = cap;
    }
    s_entries[s_count++] = *e;
    return 0;
}

/* Parse one complete line. Returns true if it is a valid next entry in the chain. */
static bool parse_line(char *line, LogEntry *e, uint64_t prev_hash, uint64_t *hash_out)
{
    char *fields[8];
    int n = 0;
    char *p = line;
    fields[n++] = p;
    while (*p && n < 8) {
        if (*p == '|') { *p = '\0'; fields[n++] = p + 1; }
        p++;
    }
    if (n != 8) return false;

    memset(e, 0, sizeof *e);
    e->seq        = (unsigned)strtoul(fields[0], NULL, 10);
    e->time_ms    = strtoll(fields[1], NULL, 10);
    copy_clean(e->adc_id, sizeof e->adc_id, fields[2]);
    e->event_type = (EventType)atoi(fields[3]);
    copy_clean(e->user_id, sizeof e->user_id, fields[4]);
    e->outcome    = (Outcome)atoi(fields[5]);
    copy_clean(e->details, sizeof e->details, fields[6]);
    if (e->event_type < 0 || e->event_type >= EVT_COUNT) return false;
    if (e->outcome < 0 || e->outcome >= OUT_COUNT) return false;

    char content[1024];
    format_content(e, content, sizeof content);
    uint64_t expect = chain_hash(prev_hash, content);
    uint64_t stored = strtoull(fields[7], NULL, 16);
    if (stored != expect) return false;     /* LOG-I6: edited or corrupted record */
    *hash_out = stored;
    return true;
}

/* ---------- SPEC-LOG-3: load ---------- */
static void load(void)
{
    FILE *f = fopen(s_path, "rb");
    if (!f) return;                                   /* row G1: no file */

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)size + 1);
    if (!buf) { fclose(f); s_healthy = false; return; }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = '\0';

    /* Row G3: a last line with no '\n' was never completed, so it was never an entry. */
    long complete = (long)got;
    while (complete > 0 && buf[complete - 1] != '\n') complete--;
    if (complete < (long)got) {
        if (truncate(s_path, complete) != 0) s_healthy = false;
        buf[complete] = '\0';
    }

    char *line = buf;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        LogEntry e;
        uint64_t h;
        if (!parse_line(line, &e, s_last_hash, &h) || e.seq != s_next_seq) {
            s_healthy = false;                        /* row G4: gap, bad hash, bad record */
            break;
        }
        if (push_entry(&e) != 0) { s_healthy = false; break; }
        s_last_hash = h;
        s_next_seq++;
        if (!nl) break;
        line = nl + 1;
    }
    free(buf);                                        /* row G2 if nothing went wrong */
}

/* ---------- public operations ---------- */
int event_log_init(const char *adc_id, const char *storage_path, NowMsFn now_ms)
{
    event_log_close();
    copy_clean(s_adc_id, sizeof s_adc_id, adc_id);
    snprintf(s_path, sizeof s_path, "%s", storage_path);
    s_now = now_ms;
    s_next_seq = 1;
    s_last_hash = 0;
    s_healthy = true;
    load();
    s_file = fopen(s_path, "ab");
    if (!s_file) s_healthy = false;
    return 0;
}

LogStatus event_log_append(EventType event_type, Outcome outcome,
                           const char *user_id, const char *details,
                           LogEntry *out)
{
    /* Row L1 */
    if (event_type < 0 || event_type >= EVT_COUNT || outcome < 0 || outcome >= OUT_COUNT)
        return LOG_ERR_INVALID;
    /* Row L2 */
    if (details_forbidden(details))
        return LOG_ERR_FORBIDDEN;

    LogEntry e;
    memset(&e, 0, sizeof e);
    e.seq = s_next_seq;
    e.time_ms = s_now ? s_now() : 0;
    if (s_count > 0 && e.time_ms < s_entries[s_count - 1].time_ms)
        e.time_ms = s_entries[s_count - 1].time_ms;   /* LOG-I2: time never goes backwards */
    snprintf(e.adc_id, sizeof e.adc_id, "%s", s_adc_id);
    e.event_type = event_type;
    e.outcome = outcome;
    copy_clean(e.user_id, sizeof e.user_id, user_id);
    copy_clean(e.details, sizeof e.details, details);

    char content[1024];
    format_content(&e, content, sizeof content);
    uint64_t h = chain_hash(s_last_hash, content);

    /* Row L3: write, flush and sync before returning (OUT-4). */
    bool ok = s_file != NULL;
#ifdef ADC_SIM
    if (s_sim_fail_writes) ok = false;
#endif
    long before = ok ? ftell(s_file) : 0;
    if (ok) ok = fprintf(s_file, "%s|%016" PRIx64 "\n", content, h) > 0;
    if (ok) ok = fflush(s_file) == 0;
    if (ok) ok = fsync(fileno(s_file)) == 0;
    if (!ok) {
        if (s_file && before >= 0) {           /* undo a partial line if possible */
            fflush(s_file);
            if (ftruncate(fileno(s_file), before) != 0) { /* left for G3 at next load */ }
        }
        s_healthy = false;
        return LOG_ERR_STORAGE;
    }
    if (push_entry(&e) != 0) { s_healthy = false; return LOG_ERR_STORAGE; }

    /* Row L4 */
    s_last_hash = h;
    s_next_seq++;
    if (out) *out = e;
    return LOG_OK;
}

int event_log_entries(unsigned from_seq, unsigned to_seq, LogEntry *out, int max)
{
    int n = 0;
    if (from_seq < 1) from_seq = 1;
    for (int i = 0; i < s_count && n < max; i++) {
        unsigned seq = s_entries[i].seq;
        if (seq < from_seq) continue;
        if (to_seq != 0 && seq > to_seq) break;
        out[n++] = s_entries[i];        /* copy: callers cannot change a stored record */
    }
    return n;
}

unsigned event_log_last_seq(void) { return s_count ? s_entries[s_count - 1].seq : 0; }

bool event_log_is_healthy(void) { return s_healthy; }

void event_log_close(void)
{
    if (s_file) { fclose(s_file); s_file = NULL; }
    free(s_entries);
    s_entries = NULL;
    s_count = s_capacity = 0;
}
