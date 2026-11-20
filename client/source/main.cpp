// homebrew-store — on-console client (v0.1) for the Switch Homebrew Store.
//
// Reads a catalog bundled via romfs (romfs:/catalog.tsv, generated from
// catalog/index.json at build time) and lets you browse apps and view details.
//
// v0.1 is browse-only. Live network fetch + download/install to the SD card is
// the next milestone (it will add the switch-curl portlib). This app itself is
// ordinary homebrew: it runs where unsigned NROs are already permitted.
//
// License: MIT.

#include <switch.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#if STORE_NETWORK
#include <curl/curl.h>
#include <sys/stat.h>
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

bool loadCatalog() {
    FILE* f = fopen("romfs:/catalog.tsv", "rb");
    if (!f) return false;
    char line[1200];
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
        if (a.id[0]) ++appCount;
    }
    fclose(f);
    return true;
}

char g_status[256] = "";

#if STORE_NETWORK
size_t writeToFile(void* ptr, size_t sz, size_t nm, void* f) {
    return fwrite(ptr, sz, nm, static_cast<FILE*>(f));
}

// Download the selected app's .nro over HTTPS and install it to
// sdmc:/switch/<id>/<id>.nro. Blocking; updates g_status with the result.
bool installApp(const App& a) {
    if (!a.download[0]) { snprintf(g_status, sizeof(g_status), "No download URL."); return false; }

    char dir[256];
    snprintf(dir, sizeof(dir), "sdmc:/switch/%s", a.id);
    mkdir("sdmc:/switch", 0777);
    mkdir(dir, 0777);

    char out[512];
    snprintf(out, sizeof(out), "%s/%s.nro", dir, a.id);

    FILE* f = fopen(out, "wb");
    if (!f) { snprintf(g_status, sizeof(g_status), "Error: cannot write to SD card."); return false; }

    CURL* c = curl_easy_init();
    if (!c) { fclose(f); remove(out); snprintf(g_status, sizeof(g_status), "Error: curl init failed."); return false; }
    curl_easy_setopt(c, CURLOPT_URL, a.download);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/cacert.pem"); // bundled CA store
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "SnaccMart/0.2");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 120L);

    CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    fclose(f);

    if (rc != CURLE_OK) {
        remove(out);
        snprintf(g_status, sizeof(g_status), "Download failed: %s", curl_easy_strerror(rc));
        return false;
    }
    snprintf(g_status, sizeof(g_status), "Installed to %s", out);
    return true;
}
#endif

void header() {
    printf("%s%s  Switch Homebrew Store%s  %sv0.1%s\n", BOLD, CYAN, RESET, DIM, RESET);
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
    printf("\n  %s%d app(s)   A: details   +: exit%s\n", DIM, appCount, RESET);
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
    printf("  %sInstalls%s  : sdmc:/switch/%s/\n", BOLD, RESET, a.id);
    if (a.assets)
        printf("\n  %s! Needs your own legally-owned data files.%s\n", YELLOW, RESET);
#if STORE_NETWORK
    if (g_status[0]) printf("\n  %s%s%s\n", GREEN, g_status, RESET);
    printf("\n  %sX: download & install to SD   B: back   +: exit%s\n", DIM, RESET);
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
    if (haveRomfs) loadCatalog();

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
                snprintf(g_status, sizeof(g_status), "Downloading...");
                clearScreen(); drawDetail(apps[sel]); consoleUpdate(NULL);
                installApp(apps[sel]); // blocking; sets g_status
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
