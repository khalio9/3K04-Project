/* comm_protocol.c - PRIVATE part of CommManager: the wire format (its secret).
 *
 * Format of one message (one text line):
 *     TYPE|adc_id|seq|time_ms|body|mac
 * mac = keyed hash of everything before it, so a message from anyone without
 * the shared key is rejected (NFR-SEC-03).
 * NOTE: the keyed FNV hash is a placeholder for the demo. A real deployment uses
 * TLS / HMAC (NFR-SEC-02); swapping it changes only this file.
 */
#include "comm_protocol.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SHARED_KEY = "3K04-G19-ADC-MS";

static const char *TYPE_NAMES[MSG_COUNT] = {
    "HEARTBEAT", "STATUS", "INVENTORY", "ALARM", "MAINTENANCE", "HEALTH", "EVENT",
    "HEARTBEAT_REPLY", "PERMISSION_UPDATE", "SCHEDULE", "CONFIG_CHANGE", "ACK"
};

const char *proto_type_name(MessageType t)
{
    return (t >= 0 && t < MSG_COUNT) ? TYPE_NAMES[t] : "UNKNOWN";
}

static int type_from_name(const char *s)
{
    for (int i = 0; i < MSG_COUNT; i++)
        if (strcmp(s, TYPE_NAMES[i]) == 0) return i;
    return -1;
}

static uint64_t mac_of(const char *text)
{
    uint64_t h = 1469598103934665603ULL;
    const char *parts[2] = { SHARED_KEY, text };
    for (int k = 0; k < 2; k++)
        for (const char *s = parts[k]; *s; s++) {
            h ^= (unsigned char)*s;
            h *= 1099511628211ULL;
        }
    return h;
}

static void clean(char *dst, int cap, const char *src)
{
    int i = 0;
    if (src)
        for (; src[i] && i + 1 < cap; i++)
            dst[i] = (src[i] == '|' || src[i] == '\n' || src[i] == '\r') ? '/' : src[i];
    dst[i] = '\0';
}

int proto_encode(const WireMsg *m, char *out, int cap)
{
    char body[BODY_MAX], text[WIRE_MAX];
    clean(body, sizeof body, m->body);
    snprintf(text, sizeof text, "%s|%s|%u|%" PRId64 "|%s",
             proto_type_name(m->type), m->adc_id, m->seq, (int64_t)m->time_ms, body);
    return snprintf(out, (size_t)cap, "%s|%016" PRIx64, text, mac_of(text));
}

bool proto_decode(const char *raw, WireMsg *m)
{
    if (!raw) return false;
    char buf[WIRE_MAX];
    size_t len = strlen(raw);
    if (len == 0 || len >= sizeof buf) return false;
    memcpy(buf, raw, len + 1);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = '\0';

    /* split off the mac (last field) and check it */
    char *bar = strrchr(buf, '|');
    if (!bar) return false;
    *bar = '\0';
    char *end = NULL;
    uint64_t mac = strtoull(bar + 1, &end, 16);
    if (end == bar + 1 || mac != mac_of(buf)) return false;      /* fails authentication */

    char *f[5];
    int n = 0;
    char *p = buf;
    f[n++] = p;
    while (*p && n < 5) { if (*p == '|') { *p = '\0'; f[n++] = p + 1; } p++; }
    if (n != 5) return false;                                    /* malformed */

    int t = type_from_name(f[0]);
    if (t < 0) return false;
    memset(m, 0, sizeof *m);
    m->type = (MessageType)t;
    clean(m->adc_id, sizeof m->adc_id, f[1]);
    m->seq = (unsigned)strtoul(f[2], NULL, 10);
    m->time_ms = strtoll(f[3], NULL, 10);
    clean(m->body, sizeof m->body, f[4]);
    return true;
}

bool proto_body_get(const char *body, const char *key, char *out, int cap)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        while (*p == ';' || *p == ' ') p++;
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            int i = 0;
            while (*p && *p != ';' && i + 1 < cap) out[i++] = *p++;
            out[i] = '\0';
            return true;
        }
        while (*p && *p != ';') p++;
    }
    return false;
}