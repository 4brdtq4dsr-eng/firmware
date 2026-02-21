#include "lab_apps.h"

#include "core/display.h"
#include "core/sd_functions.h"
#include "core/utils.h"
#include <WiFi.h>
#include <esp32-hal-psram.h>

#include <algorithm>
#include <cmath>

#ifndef BTN_ACT
#define BTN_ACT LOW
#endif

#ifndef BTN_A
#define BTN_A -1
#endif
#ifndef BTN_B
#define BTN_B -1
#endif
#ifndef BTN_MENU
#define BTN_MENU -1
#endif
#ifndef BTN_SELECT
#define BTN_SELECT -1
#endif
#ifndef BTN_START
#define BTN_START -1
#endif
#ifndef JOY_Z_BTN
#define JOY_Z_BTN -1
#endif
#ifndef JOY_X_PIN
#define JOY_X_PIN -1
#endif
#ifndef JOY_Y_PIN
#define JOY_Y_PIN -1
#endif

namespace {

bool readBtn(int pin) {
    if (pin < 0) return false;
    return digitalRead(pin) == BTN_ACT;
}

int readAxis(int pin) {
    if (pin < 0) return 2048;
    return analogRead(pin);
}

float axisNorm(int value) {
    float n = ((float)value - 2048.0f) / 2048.0f;
    if (n > 1.0f) n = 1.0f;
    if (n < -1.0f) n = -1.0f;
    return n;
}

void drawHeader(const String &title) {
    drawMainBorderWithTitle(title, false);
    tft.setTextSize(FP);
    tft.setCursor(BORDER_PAD_X, 26);
}

String wifiSecName(wifi_auth_mode_t enc) {
    switch (enc) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "UNK";
    }
}

struct ScanResult {
    String ssid;
    String bssid;
    int32_t rssi;
    int32_t channel;
    String security;
};

std::vector<ScanResult> runPassiveScan() {
    std::vector<ScanResult> res;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    int count = WiFi.scanNetworks(false, true, true, 110);
    if (count < 0) return res;

    res.reserve(count);
    for (int i = 0; i < count; i++) {
        ScanResult row;
        row.ssid = WiFi.SSID(i);
        if (row.ssid.length() == 0) row.ssid = "<hidden>";
        row.bssid = WiFi.BSSIDstr(i);
        row.rssi = WiFi.RSSI(i);
        row.channel = WiFi.channel(i);
        row.security = wifiSecName((wifi_auth_mode_t)WiFi.encryptionType(i));
        res.push_back(row);
    }

    std::sort(res.begin(), res.end(), [](const ScanResult &a, const ScanResult &b) { return a.rssi > b.rssi; });
    WiFi.scanDelete();
    return res;
}

void logScanResults(const std::vector<ScanResult> &results) {
    if (!setupSdCard()) return;

    if (!SD.exists("/wifi_scans")) SD.mkdir("/wifi_scans");

    String filename = "/wifi_scans/scan_" + String(millis()) + ".csv";
    File f = SD.open(filename, FILE_WRITE);
    if (!f) return;

    f.println("timestamp_ms,ssid,bssid,rssi,channel,security");
    unsigned long ts = millis();
    for (const auto &r : results) {
        String ssid = r.ssid;
        ssid.replace("\"", "'");
        f.printf("%lu,\"%s\",%s,%ld,%ld,%s\n", ts, ssid.c_str(), r.bssid.c_str(), r.rssi, r.channel, r.security.c_str());
    }
    f.close();
}

} // namespace

void hardware_test_app() {
    int page = 0;
    while (true) {
        if (check(EscPress) || readBtn(BTN_MENU)) return;
        if (check(NextPress)) page = (page + 1) % 3;
        if (check(PrevPress)) page = (page + 2) % 3;

        drawHeader("Hardware Test");
        if (page == 0) {
            tft.println("Input Test");
            int joyX = readAxis(JOY_X_PIN);
            int joyY = readAxis(JOY_Y_PIN);
            padprintf("JOY_X raw:%4d norm:%0.2f\n", joyX, axisNorm(joyX));
            padprintf("JOY_Y raw:%4d norm:%0.2f\n", joyY, axisNorm(joyY));
            tft.println();
            padprintf("A:%d B:%d Menu:%d\n", readBtn(BTN_A), readBtn(BTN_B), readBtn(BTN_MENU));
            padprintf("Sel:%d Start:%d JoyZ:%d\n", readBtn(BTN_SELECT), readBtn(BTN_START), readBtn(JOY_Z_BTN));
            tft.println("Next/Prev: pages");
            tft.println("Menu/Back: exit");
        } else if (page == 1) {
            tft.println("SD Test");
            bool mounted = setupSdCard();
            padprintln(String("Mounted: ") + (mounted ? "YES" : "NO"));
            if (mounted) {
                File root = SD.open("/");
                int listed = 0;
                while (root && listed < 6) {
                    File entry = root.openNextFile();
                    if (!entry) break;
                    padprintln(String(entry.isDirectory() ? "[D] " : "[F] ") + entry.name());
                    entry.close();
                    listed++;
                }
                root.close();

                File t = SD.open("/hw_test.txt", FILE_WRITE);
                if (t) {
                    t.println("Bruce HW test");
                    t.close();
                    File r = SD.open("/hw_test.txt", FILE_READ);
                    if (r) {
                        padprintln("test file: " + r.readStringUntil('\n'));
                        r.close();
                    }
                }
            } else {
                padprintln("SD not mounted.");
            }
        } else {
            tft.println("Memory / PSRAM");
            bool hasP = psramFound();
            padprintln(String("PSRAM: ") + (hasP ? "PRESENT" : "ABSENT"));
            padprintf("Heap free: %u\n", ESP.getFreeHeap());
            padprintf("PSRAM free: %u\n", ESP.getFreePsram());
            if (hasP) {
                const size_t sz = 16 * 1024;
                uint8_t *buf = (uint8_t *)ps_malloc(sz);
                if (buf) {
                    memset(buf, 0x5A, sz);
                    padprintln("PSRAM alloc 16KB: OK");
                    free(buf);
                } else {
                    padprintln("PSRAM alloc 16KB: FAIL");
                }
            }
        }

        delay(120);
    }
}

void wifi_scanner_passive_app() {
    std::vector<ScanResult> results = runPassiveScan();
    if (sdcardMounted || setupSdCard()) logScanResults(results);

    size_t index = 0;
    bool aPrev = false;

    while (true) {
        bool aNow = readBtn(BTN_A);
        if (aNow && !aPrev) {
            results = runPassiveScan();
            if (sdcardMounted || setupSdCard()) logScanResults(results);
            if (index >= results.size()) index = 0;
        }
        aPrev = aNow;

        if (check(EscPress) || readBtn(BTN_MENU)) return;
        if (check(NextPress) && !results.empty()) index = (index + 1) % results.size();
        if (check(PrevPress) && !results.empty()) index = (index + results.size() - 1) % results.size();

        drawHeader("Wi-Fi Scanner");
        padprintln("Passive scan only");
        padprintln(sdcardMounted ? "SD log: ON" : "SD not mounted");
        padprintln("A: refresh  Menu: back");

        int rows = 8;
        if (results.empty()) {
            padprintln("No networks found");
        } else {
            size_t start = (index / rows) * rows;
            for (int r = 0; r < rows && (start + r) < results.size(); r++) {
                const auto &it = results[start + r];
                String marker = (start + r == index) ? ">" : " ";
                String ssid = it.ssid;
                if (ssid.length() > 12) ssid = ssid.substring(0, 12) + "~";
                tft.printf("%s%-13s %4lddB c%ld %s\n", marker.c_str(), ssid.c_str(), it.rssi, it.channel, it.security.c_str());
            }
        }

        delay(80);
    }
}

void stalker_demo_app() {
    static const uint8_t world[12][12] = {
        {1,1,1,1,1,1,1,1,1,1,1,1},
        {1,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,1,1,0,1,1,1,1,1,0,1},
        {1,0,1,0,0,0,0,0,0,1,0,1},
        {1,0,1,0,1,1,1,1,0,1,0,1},
        {1,0,0,0,0,0,0,1,0,0,0,1},
        {1,0,1,1,1,1,0,1,1,1,0,1},
        {1,0,0,0,0,1,0,0,0,1,0,1},
        {1,1,1,1,0,1,1,1,0,1,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,1},
        {1,0,0,0,0,0,0,0,0,0,0,1},
        {1,1,1,1,1,1,1,1,1,1,1,1},
    };

    float px = 2.0f, py = 9.5f, a = -1.57f;
    bool flashlight = false;

    while (true) {
        if (check(EscPress) || readBtn(BTN_MENU)) return;

        int ax = readAxis(JOY_X_PIN);
        int ay = readAxis(JOY_Y_PIN);
        float turn = axisNorm(ax) * 0.08f;
        float speed = -axisNorm(ay) * 0.10f;
        a += turn;

        float nx = px + cosf(a) * speed;
        float ny = py + sinf(a) * speed;
        if (world[(int)ny][(int)nx] == 0) {
            px = nx;
            py = ny;
        }

        static bool aPrev = false;
        bool aNow = readBtn(BTN_A);
        if (aNow && !aPrev) flashlight = !flashlight;
        aPrev = aNow;

        drawMainBorderWithTitle("Stalker Demo", false);
        int ox = BORDER_PAD_X;
        int oy = 24;
        int w = tftWidth - BORDER_PAD_X * 2;
        int h = tftHeight - oy - BORDER_PAD_Y;
        tft.fillRect(ox, oy, w, h, TFT_BLACK);

        int cell = std::min(w / 12, h / 12);
        int mx = ox + (w - cell * 12) / 2;
        int my = oy + (h - cell * 12) / 2;

        int fogRadius = flashlight ? 5 : 3;
        int pxi = (int)(px * cell);
        int pyi = (int)(py * cell);

        for (int y = 0; y < 12; y++) {
            for (int x = 0; x < 12; x++) {
                int cx = x * cell + cell / 2;
                int cy = y * cell + cell / 2;
                int dx = cx - pxi;
                int dy = cy - pyi;
                int dist2 = dx * dx + dy * dy;
                int vis2 = (fogRadius * cell) * (fogRadius * cell);
                uint16_t c = world[y][x] ? TFT_DARKGREY : TFT_BLACK;
                if (dist2 < vis2) c = world[y][x] ? TFT_WHITE : 0x2104;
                tft.fillRect(mx + x * cell, my + y * cell, cell - 1, cell - 1, c);
            }
        }

        int pxs = mx + (int)(px * cell);
        int pys = my + (int)(py * cell);
        tft.fillCircle(pxs, pys, std::max(2, cell / 4), TFT_RED);
        int lx = pxs + (int)(cosf(a) * cell);
        int ly = pys + (int)(sinf(a) * cell);
        tft.drawLine(pxs, pys, lx, ly, TFT_YELLOW);
        tft.setTextSize(FP);
        tft.setCursor(BORDER_PAD_X, tftHeight - 10);
        tft.print("JOY move/turn  A light  Menu back");

        delay(30);
    }
}
