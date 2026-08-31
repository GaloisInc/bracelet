#include "handlers.h"

#include <magic.h>
#include <libxml/parser.h>
#include <libxml/SAX2.h>
#include <zlib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void handle_text(const unsigned char *data, size_t len);
static void handle_xml(const unsigned char *data, size_t len);
void handle_gzip(const unsigned char *data, size_t len);

// ---------------------------------------------------------------------------
// MIME detection
// ---------------------------------------------------------------------------

static const char *detect_mime(const unsigned char *data, size_t len) {
    magic_t m = magic_open(MAGIC_MIME_TYPE);
    if (!m) { fprintf(stderr, "magic_open failed\n"); exit(1); }
    if (magic_load(m, NULL) != 0) {
        fprintf(stderr, "magic_load failed: %s\n", magic_error(m));
        magic_close(m);
        exit(1);
    }
    const char *mime = magic_buffer(m, data, len);
    static char buf[256];
    strncpy(buf, mime ? mime : "", sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    magic_close(m);
    return buf;
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void dispatch_buffer(const unsigned char *data, size_t len) {
    const char *mime = detect_mime(data, len);
    if (strcmp(mime, "text/plain") == 0) {
        handle_text(data, len);
    } else if (strcmp(mime, "text/xml") == 0 || strcmp(mime, "application/xml") == 0) {
        handle_xml(data, len);
    } else if (strcmp(mime, "application/gzip") == 0) {
        handle_gzip(data, len);
    } else {
        fprintf(stderr, "Unsupported MIME type: %s\n", mime);
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// Plain text handler
// ---------------------------------------------------------------------------

static void handle_text(const unsigned char *data, size_t len) {
    fwrite(data, 1, len, stdout);
}

// ---------------------------------------------------------------------------
// XML handler
// ---------------------------------------------------------------------------

static void xml_characters(void *ctx, const xmlChar *ch, int len) {
    (void)ctx;
    fwrite(ch, 1, len, stdout);
    fputc('\n', stdout);
}

static void handle_xml(const unsigned char *data, size_t len) {
    xmlSAXHandler sax;
    memset(&sax, 0, sizeof(sax));
    sax.characters = xml_characters;
    if (xmlSAXUserParseMemory(&sax, NULL, (const char *)data, (int)len) != 0) {
        fprintf(stderr, "XML parse error\n");
        exit(1);
    }
}

// ---------------------------------------------------------------------------
// Gzip handler
// ---------------------------------------------------------------------------

void handle_gzip(const unsigned char *data, size_t len) {
    z_stream *zs = malloc(sizeof(z_stream));
    gz_header *header = NULL;
    unsigned char *extra = NULL;
    unsigned char *out_buf = NULL;
    unsigned char *result = NULL;
    if (!zs) {
        fprintf(stderr, "malloc failed\n");
        exit(1);
    }
    memset(zs, 0, sizeof(*zs));
    if (inflateInit2(zs, 15 + 16) != Z_OK) {
        free(zs);
        fprintf(stderr, "inflateInit2 failed\n"); exit(1);
    }
    header = malloc(sizeof(gz_header));
    extra = malloc(64);
    out_buf = malloc(128);
    size_t out_cap = 4096, out_len = 0;
    result = malloc(out_cap);
    if (!header || !extra || !out_buf || !result) {
        fprintf(stderr, "malloc failed\n");
        inflateEnd(zs);
        free(zs); free(header); free(extra); free(out_buf); free(result);
        exit(1);
    }
    memset(header, 0, sizeof(*header));
    header->extra = extra;
    header->extra_max = 64;
    inflateGetHeader(zs, header);

    size_t off = 0;
    int ret = Z_OK;
    do {
        size_t chunk = 128;
        if (off + chunk > len) chunk = len - off;
        if (chunk == 0) break;
        zs->avail_in = chunk;
        zs->next_in = (unsigned char *)(data + off);
        off += chunk;
        do {
            zs->avail_out = 128;
            zs->next_out = out_buf;
            ret = inflate(zs, Z_NO_FLUSH);
            if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                inflateEnd(zs);
                free(zs); free(header); free(extra);
                free(out_buf); free(result);
                fprintf(stderr, "inflate error\n"); exit(1);
            }
            size_t have = 128 - zs->avail_out;
            if (out_len + have > out_cap) {
                while (out_len + have > out_cap) out_cap *= 2;
                unsigned char *new_result = realloc(result, out_cap);
                if (!new_result) {
                    fprintf(stderr, "realloc failed\n");
                    inflateEnd(zs);
                    free(zs); free(header); free(extra);
                    free(out_buf); free(result);
                    exit(1);
                }
                result = new_result;
            }
            memcpy(result + out_len, out_buf, have);
            out_len += have;
        } while (zs->avail_out == 0);
    } while (ret != Z_STREAM_END);

    if (ret != Z_STREAM_END) {
        fprintf(stderr, "incomplete gzip stream\n");
        inflateEnd(zs);
        free(zs); free(header); free(extra); free(out_buf); free(result);
        exit(1);
    }

    inflateEnd(zs);
    free(zs);
    free(header);
    free(extra);
    free(out_buf);

    /* Recursively dispatch the decompressed data */
    dispatch_buffer(result, out_len);
    free(result);
}
