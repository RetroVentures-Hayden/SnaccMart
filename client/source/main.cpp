// homebrew-store — on-console client (v0.2) for the Switch Homebrew Store.
//
// Reads the catalog (fetched live over HTTPS when built networked, else from the
// romfs copy bundled at build time) and lets you browse apps, view details, and
// — in the networked build — download + sha256-verify + install to the SD card.
//
// Two build flavours (see the Makefile's STORE_NETWORK): the default is
// browse-only and dependency-free; STORE_NETWORK=1 adds switch-curl for fetch
// and install. Either way this is ordinary homebrew: it runs where unsigned
// NROs are already permitted.
//
// License: MIT.

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#if STORE_NETWORK
#include <curl/curl.h>
#include <sys/stat.h>
#include <strings.h>
#include <cstdint>
#include <zlib.h>
// Default live-catalog URL. Override at runtime by placing a URL in
// sdmc:/switch/homebrew-store/catalog_url.txt (e.g. point at a local mirror).
#define DEFAULT_CATALOG_URL "https://raw.githubusercontent.com/RetroVentures-Hayden/SnaccMart/main/catalog/catalog.tsv"
#endif

namespace {

constexpr const char* RESET  = "\x1b[0m";
constexpr const char* BOLD   = "\x1b[1m";
constexpr const char* DIM    = "\x1b[2m";
constexpr const char* CYAN    = "\x1b[36m";
constexpr const char* GREEN  = "\x1b[32m";
constexpr const char* YELLOW = "\x1b[33m";
constexpr const char* INVERT = "\x1b[7m";

void clearScreen() { printf("\x1b[2J\x1b[H"); }

struct App {
    char id[64]       = "";
    char name[80]     = "";
    char author[80]   = "";
    char category[24] = "";
    char version[32]  = "";
    char license[40]  = "";
    char desc[256]    = "";
    char download[512]= "";
    char source[256]  = "";
    bool assets       = false;
    char sha256[72]   = "";
    char filetype[8]  = "nro"; // "nro" or "zip"
    char binary[256]  = "";    // path to the .nro inside a zip (informational)
    bool pending      = false; // listed but not yet downloadable
};

constexpr int MAX_APPS = 512;
App apps[MAX_APPS];
int appCount = 0;

// Copy field index n (0-based) from a tab-separated line into dst.
void field(const char* line, int n, char* dst, size_t cap) {
    const char* p = line;
    for (int i = 0; i < n && p; ++i) {
        p = strchr(p, '\t');
        if (p) ++p;
    }
    if (!p) { if (cap) dst[0] = '\0'; return; }
    const char* end = strchr(p, '\t');
    size_t len = end ? static_cast<size_t>(end - p) : strlen(p);
    if (len >= cap) len = cap - 1;
    memcpy(dst, p, len);
    dst[len] = '\0';
}

// Validate an id the same way the schema does: ^[a-z0-9][a-z0-9-]{1,63}$.
// The id becomes a filesystem path on the console, so a bad one must never be
// trusted from a downloaded/redirected catalog (path-traversal defense).
bool validId(const char* s) {
    size_t n = strlen(s);
    if (n < 2 || n > 64) return false;
    char c0 = s[0];
    if (!((c0 >= 'a' && c0 <= 'z') || (c0 >= '0' && c0 <= '9'))) return false;
    for (size_t i = 1; i < n; ++i) {
        char c = s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}

bool loadCatalogPath(const char* path) {
    appCount = 0;
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    char line[2048];
    while (fgets(line, sizeof(line), f) && appCount < MAX_APPS) {
        // strip trailing newline
        size_t L = strlen(line);
        while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = '\0';
        if (L == 0) continue;
        App& a = apps[appCount];
        field(line, 0, a.id, sizeof(a.id));
        field(line, 1, a.name, sizeof(a.name));
        field(line, 2, a.author, sizeof(a.author));
        field(line, 3, a.category, sizeof(a.category));
        field(line, 4, a.version, sizeof(a.version));
        field(line, 5, a.license, sizeof(a.license));
        field(line, 6, a.desc, sizeof(a.desc));
        field(line, 7, a.download, sizeof(a.download));
        field(line, 8, a.source, sizeof(a.source));
        char flag[4]; field(line, 9, flag, sizeof(flag));
        a.assets = (flag[0] == '1');
        field(line, 10, a.sha256, sizeof(a.sha256));
        field(line, 11, a.filetype, sizeof(a.filetype));
        if (!a.filetype[0]) snprintf(a.filetype, sizeof(a.filetype), "nro");
        field(line, 12, a.binary, sizeof(a.binary));
        char pflag[4]; field(line, 13, pflag, sizeof(pflag));
        a.pending = (pflag[0] == '1');
        if (validId(a.id)) ++appCount; // skip entries with an unsafe/invalid id
    }
    fclose(f);
    return appCount > 0;
}

char g_status[256] = "";
bool g_online = false;

#if STORE_NETWORK
size_t writeToFile(void* ptr, size_t sz, size_t nm, void* f) {
    return fwrite(ptr, sz, nm, static_cast<FILE*>(f));
}

// HTTPS GET to a file. Returns true on a clean download.
bool curlDownload(const char* url, const char* outpath) {
    FILE* f = fopen(outpath, "wb");
    if (!f) return false;
    CURL* c = curl_easy_init();
    if (!c) { fclose(f); remove(outpath); return false; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "SnaccMart/0.2");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 300L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);
    if (rc != CURLE_OK) { remove(outpath); return false; }
    return true;
}

// Catalog URL: an optional override on the SD card, else the default.
// HTTPS only — an unauthenticated (http://) catalog can be swapped in transit,
// sha256 hashes included, so a non-https override is ignored.
void getCatalogUrl(char* url, size_t cap) {
    FILE* cf = fopen("sdmc:/switch/homebrew-store/catalog_url.txt", "rb");
    if (cf) {
        if (fgets(url, cap, cf)) {
            size_t L = strlen(url);
            while (L && (url[L-1] == '\n' || url[L-1] == '\r' || url[L-1] == ' ')) url[--L] = '\0';
        } else {
            url[0] = '\0';
        }
        fclose(cf);
        if (url[0] && strncasecmp(url, "https://", 8) == 0) return; // accept https overrides only
    }
    snprintf(url, cap, "%s", DEFAULT_CATALOG_URL);
}

// Fetch the live catalog TSV to a file. Returns true on a clean HTTP download.
bool fetchCatalogToFile(const char* url, const char* outpath) {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/homebrew-store", 0777);
    FILE* f = fopen(outpath, "wb");
    if (!f) return false;
    CURL* c = curl_easy_init();
    if (!c) { fclose(f); return false; }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "SnaccMart/0.2");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);
    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);
    return rc == CURLE_OK;
}

// --- SHA-256 (FIPS 180-4), original compact implementation ---
struct Sha256Ctx { uint32_t h[8]; uint64_t total; uint8_t buf[64]; uint32_t n; };

inline uint32_t rotr(uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }

void sha256_init(Sha256Ctx* c) {
    static const uint32_t iv[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
    memcpy(c->h, iv, sizeof(iv));
    c->total = 0; c->n = 0;
}

void sha256_block(Sha256Ctx* c, const uint8_t* p) {
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
    uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = (uint32_t(p[i*4]) << 24) | (uint32_t(p[i*4+1]) << 16) | (uint32_t(p[i*4+2]) << 8) | p[i*4+3];
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i-15],7) ^ rotr(w[i-15],18) ^ (w[i-15] >> 3);
        uint32_t s1 = rotr(w[i-2],17) ^ rotr(w[i-2],19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    uint32_t a=c->h[0],b=c->h[1],cc=c->h[2],d=c->h[3],e=c->h[4],f=c->h[5],g=c->h[6],hh=c->h[7];
    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = hh + S1 + ch + k[i] + w[i];
        uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        hh=g; g=f; f=e; e=d+t1; d=cc; cc=b; b=a; a=t1+t2;
    }
    c->h[0]+=a; c->h[1]+=b; c->h[2]+=cc; c->h[3]+=d; c->h[4]+=e; c->h[5]+=f; c->h[6]+=g; c->h[7]+=hh;
}

void sha256_update(Sha256Ctx* c, const uint8_t* data, size_t len) {
    c->total += len;
    while (len) {
        uint32_t take = 64 - c->n;
        if (take > len) take = static_cast<uint32_t>(len);
        memcpy(c->buf + c->n, data, take);
        c->n += take; data += take; len -= take;
        if (c->n == 64) { sha256_block(c, c->buf); c->n = 0; }
    }
}

void sha256_hex(Sha256Ctx* c, char out[65]) {
    uint64_t bits = c->total * 8;
    uint8_t pad = 0x80; sha256_update(c, &pad, 1);
    uint8_t zero = 0; while (c->n != 56) sha256_update(c, &zero, 1);
    uint8_t lenb[8]; for (int i = 0; i < 8; ++i) lenb[i] = uint8_t(bits >> (56 - 8*i));
    sha256_update(c, lenb, 8);
    static const char* hx = "0123456789abcdef";
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) {
            uint8_t byte = uint8_t(c->h[i] >> (24 - 8*j));
            out[(i*4+j)*2]   = hx[byte >> 4];
            out[(i*4+j)*2+1] = hx[byte & 15];
        }
    out[64] = '\0';
}

bool sha256File(const char* path, char hexout[65]) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    Sha256Ctx c; sha256_init(&c);
    uint8_t buf[4096]; size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0) sha256_update(&c, buf, r);
    fclose(f);
    sha256_hex(&c, hexout);
    return true;
}

// --- Minimal ZIP extractor (zlib raw inflate) ---
inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Reject absolute paths, backslashes, and any ".." component (path traversal).
bool zipNameSafe(const char* n) {
    if (!n[0] || n[0] == '/' || n[0] == '\\') return false;
    if (strchr(n, '\\')) return false;
    const char* p = n;
    while (p) {
        const char* slash = strchr(p, '/');
        size_t len = slash ? size_t(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.') return false;
        if (!slash) break;
        p = slash + 1;
    }
    return true;
}

void makeParentDirs(const char* fullpath) {
    char tmp[1200];
    snprintf(tmp, sizeof(tmp), "%s", fullpath);
    for (char* p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0777); *p = '/'; }
    }
}

// Write one zip entry, verifying its CRC-32 and catching write errors.
bool zipWriteEntry(FILE* z, long dataOff, uint32_t compSize, uint16_t method,
                   uint32_t expectedCrc, const char* outpath) {
    size_t L = strlen(outpath);
    makeParentDirs(outpath);
    if (L && outpath[L-1] == '/') { mkdir(outpath, 0777); return true; } // directory entry

    FILE* out = fopen(outpath, "wb");
    if (!out) return false;
    if (fseek(z, dataOff, SEEK_SET) != 0) { fclose(out); remove(outpath); return false; }

    bool ok = true;
    uLong crc = crc32(0L, Z_NULL, 0);

    if (method == 0) { // stored
        uint8_t buf[16384];
        uint32_t left = compSize;
        while (left) {
            size_t take = left < sizeof(buf) ? left : sizeof(buf);
            size_t r = fread(buf, 1, take, z);
            if (r == 0) { ok = false; break; }
            if (fwrite(buf, 1, r, out) != r) { ok = false; break; } // write error (e.g. disk full)
            crc = crc32(crc, buf, uInt(r));
            left -= uint32_t(r);
        }
    } else if (method == 8) { // deflate (raw)
        z_stream s; memset(&s, 0, sizeof(s));
        if (inflateInit2(&s, -MAX_WBITS) != Z_OK) { fclose(out); remove(outpath); return false; }
        uint8_t in[16384], o[16384];
        uint32_t left = compSize;
        int ret = Z_OK;
        do {
            if (s.avail_in == 0 && left > 0) {
                size_t take = left < sizeof(in) ? left : sizeof(in);
                size_t r = fread(in, 1, take, z);
                if (r == 0) { ok = false; break; }
                left -= uint32_t(r);
                s.next_in = in; s.avail_in = uInt(r);
            }
            s.next_out = o; s.avail_out = sizeof(o);
            ret = inflate(&s, Z_NO_FLUSH);
            if (ret == Z_NEED_DICT || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR || ret == Z_STREAM_ERROR) { ok = false; break; }
            size_t have = sizeof(o) - s.avail_out;
            if (have) {
                if (fwrite(o, 1, have, out) != have) { ok = false; break; }
                crc = crc32(crc, o, uInt(have));
            }
        } while (ret != Z_STREAM_END);
        inflateEnd(&s);
    } else {
        ok = false; // unsupported compression method
    }

    if (ok && (fflush(out) != 0 || ferror(out))) ok = false; // catch buffered write errors
    fclose(out);
    if (ok && uint32_t(crc) != expectedCrc) ok = false;       // integrity check
    if (!ok) remove(outpath);
    return ok;
}

// Extract a zip to outdir. Returns the number of files written, or -1 on error.
int extractZip(const char* zippath, const char* outdir) {
    FILE* z = fopen(zippath, "rb");
    if (!z) return -1;
    fseek(z, 0, SEEK_END);
    long fsize = ftell(z);
    if (fsize < 22) { fclose(z); return -1; }

    long scan = fsize < 66000 ? fsize : 66000;
    uint8_t* tail = static_cast<uint8_t*>(malloc(scan));
    if (!tail) { fclose(z); return -1; }
    fseek(z, fsize - scan, SEEK_SET);
    if (fread(tail, 1, scan, z) != size_t(scan)) { free(tail); fclose(z); return -1; }

    long eocd = -1;
    for (long i = scan - 22; i >= 0; --i) { if (rd32(tail + i) == 0x06054b50u) { eocd = i; break; } }
    if (eocd < 0) { free(tail); fclose(z); return -1; }
    uint16_t total = rd16(tail + eocd + 10);
    uint32_t cdoff = rd32(tail + eocd + 16);
    free(tail);

    // Track the files we create so a failure removes ONLY those — never a broad
    // delete, since extraction may target the SD root.
    constexpr int TRACK_CAP = 8192;
    char* created[TRACK_CAP];
    int ncreated = 0;
    int written = 0;
    bool failed = false;

    if (fseek(z, cdoff, SEEK_SET) != 0) { fclose(z); return -1; }
    for (uint16_t i = 0; i < total && !failed; ++i) {
        uint8_t hdr[46];
        if (fread(hdr, 1, 46, z) != 46 || rd32(hdr) != 0x02014b50u) break;
        uint16_t method   = rd16(hdr + 10);
        uint32_t expCrc   = rd32(hdr + 16);
        uint32_t compSize = rd32(hdr + 20);
        uint16_t nameLen  = rd16(hdr + 28);
        uint16_t extraLen = rd16(hdr + 30);
        uint16_t cmtLen   = rd16(hdr + 32);
        uint32_t lho      = rd32(hdr + 42);

        char name[1024];
        uint16_t nl = nameLen < sizeof(name) - 1 ? nameLen : sizeof(name) - 1;
        if (fread(name, 1, nl, z) != nl) break;
        name[nl] = '\0';
        if (nl < nameLen) fseek(z, nameLen - nl, SEEK_CUR);
        fseek(z, extraLen + cmtLen, SEEK_CUR);
        long nextCd = ftell(z);

        // Local header -> data offset.
        uint8_t lh[30];
        if (fseek(z, lho, SEEK_SET) == 0 && fread(lh, 1, 30, z) == 30 && rd32(lh) == 0x04034b50u) {
            uint16_t lNameLen  = rd16(lh + 26);
            uint16_t lExtraLen = rd16(lh + 28);
            long dataOff = long(lho) + 30 + lNameLen + lExtraLen;
            if (zipNameSafe(name)) {
                char outp[1200];
                snprintf(outp, sizeof(outp), "%s/%s", outdir, name);
                if (zipWriteEntry(z, dataOff, compSize, method, expCrc, outp)) {
                    ++written;
                    size_t ol = strlen(outp);
                    if (ncreated < TRACK_CAP && ol && outp[ol-1] != '/') {
                        char* dup = static_cast<char*>(malloc(ol + 1)); // track files only
                        if (dup) { memcpy(dup, outp, ol + 1); created[ncreated++] = dup; }
                    }
                } else {
                    failed = true;
                }
            }
        }
        fseek(z, nextCd, SEEK_SET);
    }
    fclose(z);

    if (failed) {
        for (int i = 0; i < ncreated; ++i) { remove(created[i]); free(created[i]); }
        return -1;
    }
    for (int i = 0; i < ncreated; ++i) free(created[i]);
    return written;
}

// Download the selected app and install it to sdmc:/switch/<id>/, verifying
// sha256 when the catalog provides one. Handles .nro (direct) and .zip
// (extracted). Blocking; updates g_status with the result.
bool installApp(const App& a) {
    if (!a.download[0]) { snprintf(g_status, sizeof(g_status), "No download URL."); return false; }

    char dir[256];
    snprintf(dir, sizeof(dir), "sdmc:/switch/%s", a.id);
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/homebrew-store", 0777);
    bool isZip = (strcasecmp(a.filetype, "zip") == 0);

    // Download target: a temp .zip to extract, or the .nro into /switch/<id>/.
    char dl[640];
    if (isZip) {
        snprintf(dl, sizeof(dl), "sdmc:/switch/homebrew-store/%s.zip", a.id);
    } else {
        mkdir(dir, 0777);
        snprintf(dl, sizeof(dl), "%s/%s.nro", dir, a.id);
    }

    if (!curlDownload(a.download, dl)) {
        snprintf(g_status, sizeof(g_status), "Download failed.");
        return false;
    }

    // Verify the downloaded file (zip or nro) against the catalog checksum.
    if (a.sha256[0]) {
        char got[65];
        if (!sha256File(dl, got) || strcasecmp(got, a.sha256) != 0) {
            remove(dl);
            snprintf(g_status, sizeof(g_status), "CHECKSUM MISMATCH - file removed.");
            return false;
        }
    }

    if (isZip) {
        // Homebrew zips carry their own full paths (atmosphere/, switch/, ...),
        // so they extract to the SD root. Rollback removes only our own files.
        int n = extractZip(dl, "sdmc:");
        remove(dl);
        if (n < 0) { snprintf(g_status, sizeof(g_status), "Install failed (bad/partial zip)."); return false; }
        snprintf(g_status, sizeof(g_status), "Installed%s: %d file(s) to SD root",
                 a.sha256[0] ? " + verified" : " (unverified)", n);
        return true;
    }

    snprintf(g_status, sizeof(g_status), "Installed%s: %s",
             a.sha256[0] ? " + verified" : " (no checksum)", dl);
    return true;
}
#endif

void header() {
    printf("%s%s  Switch Homebrew Store%s  %sv0.2%s\n", BOLD, CYAN, RESET, DIM, RESET);
    for (int i = 0; i < 80; ++i) putchar('-');
    printf("\n\n");
}

// Word-wrap a string into the console width, indented.
void wrapPrint(const char* s, int width, const char* indent) {
    int col = 0;
    printf("%s", indent);
    for (const char* p = s; *p; ++p) {
        putchar(*p);
        if (++col >= width) { printf("\n%s", indent); col = 0; }
    }
    printf("\n");
}

void drawList(int sel, int top) {
    header();
    if (appCount == 0) {
        printf("  %sNo catalog found.%s\n\n", YELLOW, RESET);
        printf("  %sromfs:/catalog.tsv is missing or empty. It is generated\n", DIM);
        printf("  from catalog/index.json at build time.%s\n", RESET);
        return;
    }
    const int VIS = 32;
    for (int i = top; i < appCount && i < top + VIS; ++i) {
        bool s = (i == sel);
        if (s) printf("%s", INVERT);
        printf("  %-20.20s %s%-10.10s%s %s\n",
               apps[i].name,
               s ? "" : DIM, apps[i].category, s ? "" : RESET,
               s ? "" : "");
        if (s) printf("%s", RESET);
    }
    printf("\n  %s%d app(s)", DIM, appCount);
#if STORE_NETWORK
    printf("   catalog: %s%s%s", g_online ? GREEN : YELLOW,
           g_online ? "online" : "bundled (offline)", DIM);
#endif
    printf("   A: details   +: exit%s\n", RESET);
}

void drawDetail(const App& a) {
    header();
    printf("  %s%s%s  %s%s%s\n", BOLD, a.name, RESET, DIM, a.version, RESET);
    printf("  %sby %s   ·   %s%s\n\n", DIM, a.author, a.category, RESET);
    printf("  %sDescription%s\n", BOLD, RESET);
    wrapPrint(a.desc[0] ? a.desc : "(none)", 72, "    ");
    printf("\n  %sLicense%s   : %s\n", BOLD, RESET, a.license);
    printf("  %sSource%s    : %s%s%s\n", BOLD, RESET, CYAN, a.source, RESET);
    printf("  %sDownload%s  : %s%s%s\n", BOLD, RESET, CYAN, a.download, RESET);
    if (strcmp(a.filetype, "zip") == 0)
        printf("  %sInstalls%s  : SD card (package, extracted to root)\n", BOLD, RESET);
    else
        printf("  %sInstalls%s  : sdmc:/switch/%s/\n", BOLD, RESET, a.id);
#if STORE_NETWORK
    printf("  %sChecksum%s  : %s\n", BOLD, RESET,
           a.sha256[0] ? "sha256 on file (verified on install)" : "none (install unverified)");
#endif
    if (a.assets)
        printf("\n  %s! Needs your own legally-owned data files.%s\n", YELLOW, RESET);
#if STORE_NETWORK
    if (a.pending) {
        printf("\n  %sComing soon - not yet downloadable.%s\n", YELLOW, RESET);
        printf("\n  %sB: back   +: exit%s\n", DIM, RESET);
    } else {
        if (g_status[0]) printf("\n  %s%s%s\n", GREEN, g_status, RESET);
        printf("\n  %sX: download & install to SD   B: back   +: exit%s\n", DIM, RESET);
    }
#else
    printf("\n  %sInstall from the console needs the networked build (v0.2).\n", DIM);
    printf("  For now, download from the Source/Download URL on a PC.%s\n", RESET);
    printf("\n  %sB: back   +: exit%s\n", DIM, RESET);
#endif
}

} // namespace

int main(int argc, char** argv) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);

#if STORE_NETWORK
    socketInitializeDefault();
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

    bool haveRomfs = R_SUCCEEDED(romfsInit());
    bool loaded = false;
#if STORE_NETWORK
    {
        char url[512];
        getCatalogUrl(url, sizeof(url));
        if (fetchCatalogToFile(url, "sdmc:/switch/homebrew-store/catalog.tsv") &&
            loadCatalogPath("sdmc:/switch/homebrew-store/catalog.tsv")) {
            g_online = true;
            loaded = true;
        }
    }
#endif
    if (!loaded && haveRomfs) loadCatalogPath("romfs:/catalog.tsv"); // bundled fallback

    int sel = 0, top = 0;
    bool detail = false;
    bool redraw = true;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        if (kDown & HidNpadButton_Plus) break;

        if (!detail) {
            if (kDown & HidNpadButton_Down) { if (sel < appCount - 1) sel++; redraw = true; }
            if (kDown & HidNpadButton_Up)   { if (sel > 0) sel--;           redraw = true; }
            if ((kDown & HidNpadButton_A) && appCount > 0) { detail = true;  redraw = true; }
            const int VIS = 32;
            if (sel < top) top = sel;
            if (sel >= top + VIS) top = sel - VIS + 1;
        } else {
            if (kDown & HidNpadButton_B) { detail = false; g_status[0] = '\0'; redraw = true; }
#if STORE_NETWORK
            if (kDown & HidNpadButton_X) {
                if (apps[sel].pending) {
                    snprintf(g_status, sizeof(g_status), "Not yet available.");
                } else {
                    snprintf(g_status, sizeof(g_status), "Downloading...");
                    clearScreen(); drawDetail(apps[sel]); consoleUpdate(NULL);
                    installApp(apps[sel]); // blocking; sets g_status
                }
                redraw = true;
            }
#endif
        }

        if (redraw) {
            clearScreen();
            if (detail) drawDetail(apps[sel]);
            else        drawList(sel, top);
            redraw = false;
        }
        consoleUpdate(NULL);
    }

    if (haveRomfs) romfsExit();
#if STORE_NETWORK
    curl_global_cleanup();
    socketExit();
#endif
    consoleExit(NULL);
    return 0;
}
