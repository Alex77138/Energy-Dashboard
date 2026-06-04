#include "sd_logger.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>

static const int SD_PIN_CS   = 10;
static const int SD_PIN_MOSI = 11;
static const int SD_PIN_CLK  = 12;
static const int SD_PIN_MISO = 13;

static const char *LOG_FILE    = "/energy_log.csv";
static const char *CSV_HEADER  = "ts,gp_w,gk_kwh,sp_w,sk_kwh";
static const char *DAILY_FILE  = "/daily2.bin";
static const char *RING_FILE   = "/dayring.bin";
static const char *PERIOD_FILE = "/period.bin";
static const long  UNIX_2024   = 1704067200L;

static bool s_ready = false;

bool sd_init() {
    SPI.begin(SD_PIN_CLK, SD_PIN_MISO, SD_PIN_MOSI, SD_PIN_CS);
    if (!SD.begin(SD_PIN_CS)) {
        Serial.println("[sd] card not found");
        s_ready = false;
        return false;
    }
    s_ready = true;
    Serial.println("[sd] card ready");
    return true;
}

bool sd_ready() { return s_ready; }

uint64_t sd_used_bytes()  { return s_ready ? SD.usedBytes()  : 0; }
uint64_t sd_total_bytes() { return s_ready ? SD.totalBytes() : 0; }

void sd_log(const AppData &d) {
    if (!s_ready) return;

    char ts_str[24];
    time_t now = time(nullptr);
    if (now >= UNIX_2024) {
        snprintf(ts_str, sizeof(ts_str), "%ld", (long)now);
    } else {
        snprintf(ts_str, sizeof(ts_str), "U%lu", millis() / 1000UL);
    }

    bool write_header = !SD.exists(LOG_FILE);
    File f = SD.open(LOG_FILE, FILE_APPEND);
    if (!f) {
        Serial.println("[sd] failed to open log file");
        return;
    }
    if (write_header) f.println(CSV_HEADER);

    char row[128];
    snprintf(row, sizeof(row), "%s,%.2f,%.4f,%.2f,%.4f",
        ts_str, d.grid.power_w, d.grid.today_kwh,
        d.solar.power_w, d.solar.today_kwh);
    f.println(row);
    f.close();
}

// Format daily2.bin v2 (0xDB): magic(1) + yday(4) + grid_base(4) + n_solar(1) + solar_base[n](4 each) + n_router(1) + router_base[n](4 each)
// Format daily2.bin v1 (0xDA): magic(1) + yday(4) + grid_base(4) + n_solar(1) + solar_base[n](4 each)
bool sd_save_daily(int yday, float grid_base, const float *solar_base, int n_solar,
                   const float *router_base, int n_router) {
    if (!s_ready) return false;
    File f = SD.open(DAILY_FILE, FILE_WRITE);
    if (!f) return false;
    uint8_t magic = 0xDB;
    uint8_t ns = (uint8_t)n_solar;
    uint8_t nr = (uint8_t)n_router;
    f.write(&magic, 1);
    f.write((uint8_t*)&yday,      sizeof(yday));
    f.write((uint8_t*)&grid_base, sizeof(grid_base));
    f.write(&ns, 1);
    for (int i = 0; i < n_solar; i++)
        f.write((uint8_t*)&solar_base[i], sizeof(float));
    f.write(&nr, 1);
    for (int i = 0; i < n_router; i++)
        f.write((uint8_t*)&router_base[i], sizeof(float));
    f.close();
    return true;
}

bool sd_load_daily(int *yday, float *grid_base, float *solar_base, int n_solar,
                   float *router_base, int n_router) {
    if (!s_ready || !SD.exists(DAILY_FILE)) return false;
    File f = SD.open(DAILY_FILE, FILE_READ);
    if (!f) return false;
    uint8_t magic = 0;
    f.read(&magic, 1);
    if (magic != 0xDA && magic != 0xDB) { f.close(); return false; }
    bool ok = f.read((uint8_t*)yday,      sizeof(int))   == sizeof(int)   &&
              f.read((uint8_t*)grid_base, sizeof(float)) == sizeof(float);
    if (!ok) { f.close(); return false; }
    uint8_t ns = 0;
    f.read(&ns, 1);
    int to_read = (ns < (uint8_t)n_solar) ? ns : n_solar;
    for (int i = 0; i < to_read; i++)
        f.read((uint8_t*)&solar_base[i], sizeof(float));
    // Sauter les entrées solaires supplémentaires si le fichier en a plus
    for (int i = to_read; i < (int)ns; i++) {
        float dummy; f.read((uint8_t*)&dummy, sizeof(float));
    }
    if (magic == 0xDB && router_base && n_router > 0) {
        uint8_t nr = 0;
        f.read(&nr, 1);
        int tr = (nr < (uint8_t)n_router) ? nr : n_router;
        for (int i = 0; i < tr; i++)
            f.read((uint8_t*)&router_base[i], sizeof(float));
    }
    f.close();
    return true;
}

bool sd_save_period(const PeriodData &pd) {
    if (!s_ready) return false;
    File f = SD.open(PERIOD_FILE, FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&pd, sizeof(PeriodData));
    f.close();
    return true;
}

bool sd_load_period(PeriodData &pd) {
    if (!s_ready || !SD.exists(PERIOD_FILE)) return false;
    File f = SD.open(PERIOD_FILE, FILE_READ);
    if (!f) return false;
    bool ok = f.read((uint8_t*)&pd, sizeof(PeriodData)) == sizeof(PeriodData);
    f.close();
    return ok;
}

bool sd_save_day_ring(int yday, int count, int head, int32_t last_ts,
                      const void *data, size_t data_sz) {
    if (!s_ready) return false;
    File f = SD.open(RING_FILE, FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&yday,    sizeof(yday));
    f.write((uint8_t*)&count,   sizeof(count));
    f.write((uint8_t*)&head,    sizeof(head));
    f.write((uint8_t*)&last_ts, sizeof(last_ts));
    f.write((uint8_t*)data, data_sz);
    f.close();
    return true;
}

bool sd_load_day_ring(int *yday, int *count, int *head, int32_t *last_ts,
                      void *data, size_t data_sz) {
    if (!s_ready || !SD.exists(RING_FILE)) return false;
    File f = SD.open(RING_FILE, FILE_READ);
    if (!f) return false;
    bool ok = f.read((uint8_t*)yday,    sizeof(int))     == sizeof(int)     &&
              f.read((uint8_t*)count,   sizeof(int))     == sizeof(int)     &&
              f.read((uint8_t*)head,    sizeof(int))     == sizeof(int)     &&
              f.read((uint8_t*)last_ts, sizeof(int32_t)) == sizeof(int32_t) &&
              f.read((uint8_t*)data, data_sz)            == (int)data_sz;
    f.close();
    return ok;
}

bool sd_get_history(char *out, size_t out_sz, int count) {
    if (!s_ready) {
        if (out_sz > 2) { out[0] = '['; out[1] = ']'; out[2] = '\0'; }
        return true;
    }
    if (count > 500) count = 500;

    File f = SD.open(LOG_FILE, FILE_READ);
    if (!f) {
        if (out_sz > 2) { out[0] = '['; out[1] = ']'; out[2] = '\0'; }
        return true;
    }
    if (f.size() == 0) {
        f.close();
        if (out_sz > 2) { out[0] = '['; out[1] = ']'; out[2] = '\0'; }
        return true;
    }

    static size_t offsets[500];
    int ring_w = 0, total = 0;
    bool bol = true, header_done = false;
    size_t byte_pos = 0;

    f.seek(0);
    while (f.available()) {
        char c = (char)f.read();
        if (c == '\r') { byte_pos++; continue; }
        if (bol && c != '\n') {
            bol = false;
            if (!header_done) {
                header_done = true;
            } else {
                offsets[ring_w] = byte_pos;
                ring_w = (ring_w + 1) % 500;
                total++;
            }
        }
        if (c == '\n') bol = true;
        byte_pos++;
    }

    if (total == 0) {
        f.close();
        if (out_sz > 2) { out[0] = '['; out[1] = ']'; out[2] = '\0'; }
        return true;
    }

    int in_ring  = (total < 500) ? total : 500;
    int actual   = (in_ring < count) ? in_ring : count;
    int oldest   = (total > 500) ? ring_w : 0;
    int skip     = in_ring - actual;
    int ring_start = (oldest + skip) % 500;

    size_t pos = 0;
    if (pos + 1 >= out_sz) { f.close(); return false; }
    out[pos++] = '[';

    static char line_buf[128];
    bool first_out = true;
    for (int i = 0; i < actual; i++) {
        f.seek(offsets[(ring_start + i) % 500]);
        int lpos = 0;
        while (f.available() && lpos < 127) {
            char c = (char)f.read();
            if (c == '\n' || c == '\r') break;
            line_buf[lpos++] = c;
        }
        line_buf[lpos] = '\0';
        if (lpos == 0) continue;

        char ts[32]={}, gp[16]={}, gk[16]={}, sp[16]={}, sk[16]={};
        if (sscanf(line_buf, "%31[^,],%15[^,],%15[^,],%15[^,],%15s",
                   ts, gp, gk, sp, sk) != 5) continue;

        char entry[160];
        int n;
        if (ts[0] == 'U')
            n = snprintf(entry, sizeof(entry),
                "{\"ts\":\"%s\",\"gp\":%s,\"gk\":%s,\"sp\":%s,\"sk\":%s}",
                ts, gp, gk, sp, sk);
        else
            n = snprintf(entry, sizeof(entry),
                "{\"ts\":%s,\"gp\":%s,\"gk\":%s,\"sp\":%s,\"sk\":%s}",
                ts, gp, gk, sp, sk);

        size_t needed = n + (first_out ? 0 : 1);
        if (pos + needed + 1 >= out_sz) break;
        if (!first_out) out[pos++] = ',';
        first_out = false;
        memcpy(out + pos, entry, n);
        pos += n;
    }

    f.close();
    if (pos + 1 >= out_sz) return false;
    out[pos++] = ']';
    out[pos]   = '\0';
    return true;
}
