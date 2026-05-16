#include "sd_logger.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>

static const int SD_PIN_CS   = 10;  // Chip Select
static const int SD_PIN_MOSI = 11;  // Master Out Slave In
static const int SD_PIN_CLK  = 12;  // Clock
static const int SD_PIN_MISO = 13;  // Master In Slave Out

static const char *LOG_FILE   = "/energy_log.csv";
static const char *CSV_HEADER = "ts,gp_w,gk_kwh,sp_w,sk_kwh";
static const char *DAILY_FILE = "/daily.bin";
static const char *RING_FILE  = "/dayring.bin";
static const long  UNIX_2024 = 1704067200L;

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

bool sd_ready() {
    return s_ready;
}

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

    if (write_header) {
        f.println(CSV_HEADER);
    }

    char row[128];
    snprintf(row, sizeof(row), "%s,%.2f,%.4f,%.2f,%.4f",
        ts_str,
        d.grid.power_w,
        d.grid.today_kwh,
        d.solar.power_w,
        d.solar.today_kwh);
    f.println(row);
    f.close();
}

bool sd_save_daily(int yday, float grid_base, float solar_base) {
    if (!s_ready) return false;
    File f = SD.open(DAILY_FILE, FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&yday,       sizeof(yday));
    f.write((uint8_t*)&grid_base,  sizeof(grid_base));
    f.write((uint8_t*)&solar_base, sizeof(solar_base));
    f.close();
    return true;
}

bool sd_load_daily(int *yday, float *grid_base, float *solar_base) {
    if (!s_ready || !SD.exists(DAILY_FILE)) return false;
    File f = SD.open(DAILY_FILE, FILE_READ);
    if (!f) return false;
    bool ok = f.read((uint8_t*)yday,       sizeof(int))   == sizeof(int)   &&
              f.read((uint8_t*)grid_base,  sizeof(float)) == sizeof(float) &&
              f.read((uint8_t*)solar_base, sizeof(float)) == sizeof(float);
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

    size_t file_size = f.size();
    if (file_size == 0) {
        f.close();
        if (out_sz > 2) { out[0] = '['; out[1] = ']'; out[2] = '\0'; }
        return true;
    }

    static char line_buf[128];
    static char lines[500][128];
    int line_count = 0;
    int buf_pos = 0;

    f.seek(0);
    bool first_line = true;

    while (f.available()) {
        char c = f.read();
        if (c == '\n' || c == '\r') {
            if (buf_pos > 0) {
                line_buf[buf_pos] = '\0';
                buf_pos = 0;
                if (first_line) {
                    first_line = false;
                    continue;
                }
                if (line_count < 500) {
                    strncpy(lines[line_count], line_buf, 127);
                    lines[line_count][127] = '\0';
                    line_count++;
                } else {
                    for (int i = 0; i < 499; i++) {
                        memcpy(lines[i], lines[i+1], 128);
                    }
                    strncpy(lines[499], line_buf, 127);
                    lines[499][127] = '\0';
                }
            }
        } else {
            if (buf_pos < 127) {
                line_buf[buf_pos++] = c;
            }
        }
    }
    f.close();

    int start = (line_count > count) ? (line_count - count) : 0;
    int actual = line_count - start;

    size_t pos = 0;
    if (pos + 1 >= out_sz) return false;
    out[pos++] = '[';

    for (int i = start; i < line_count; i++) {
        char ts_str[32]  = {0};
        char gp_str[16]  = {0};
        char gk_str[16]  = {0};
        char sp_str[16]  = {0};
        char sk_str[16]  = {0};

        if (sscanf(lines[i], "%31[^,],%15[^,],%15[^,],%15[^,],%15s",
                   ts_str, gp_str, gk_str, sp_str, sk_str) != 5) {
            continue;
        }

        char entry[160];
        bool is_uptime = (ts_str[0] == 'U');
        if (is_uptime) {
            snprintf(entry, sizeof(entry),
                "{\"ts\":\"%s\",\"gp\":%s,\"gk\":%s,\"sp\":%s,\"sk\":%s}",
                ts_str, gp_str, gk_str, sp_str, sk_str);
        } else {
            snprintf(entry, sizeof(entry),
                "{\"ts\":%s,\"gp\":%s,\"gk\":%s,\"sp\":%s,\"sk\":%s}",
                ts_str, gp_str, gk_str, sp_str, sk_str);
        }

        size_t entry_len = strlen(entry);
        bool last = (i == line_count - 1);
        size_t needed = entry_len + (last ? 0 : 1);

        if (pos + needed + 1 >= out_sz) return false;
        memcpy(out + pos, entry, entry_len);
        pos += entry_len;
        if (!last) out[pos++] = ',';
    }

    if (pos + 1 >= out_sz) return false;
    out[pos++] = ']';
    out[pos]   = '\0';
    return true;
}
