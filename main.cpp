/*
 * main.cpp - part of PIM670 Zabbix Display
 */

/* Standard header files. */

#include <algorithm>
#include <cmath> // for std::ceil
#include <stdio.h>
#include <stdlib.h>

/* SDK header files. */

#include "hardware/watchdog.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

/* Local header files. */

#include "opt/config.h"
#include "opt/httpclient.h"
#include "opt/internals.h"
#include "usbfs.h"

/* Stuff from pimoroni example */

#include "cosmic_unicorn.hpp"
#include "libraries/pico_graphics/pico_graphics.hpp"

/* Our stuff. */

#include <string>
#include "doom_alert.h"
#include "doom_resolved.h"

typedef enum States
{
    ST_DO_REQUEST = 0,
    ST_WAIT_RESPONSE,
    ST_HANDLE_RESPONSE,
    ST_TRANSITION,
    ST_SLEEP
} State;

std::vector<std::string> split(const std::string& s);

class ZabbixAlert
{
public:
    ZabbixAlert(
        uint32_t clock, uint32_t hostid, uint8_t severity, uint8_t suppressed)
        : clock(clock), hostid(hostid), severity(severity),
          suppressed(suppressed)
    {
    }

    static ZabbixAlert from_csv(const std::string& s)
    {
        /* "<time>;<severity>;<suppr>;<hostid>;<hostname>;<message>" */
        std::vector<std::string> result = split(s);
        if (result.size() == 6)
        {
            return ZabbixAlert(
                static_cast<uint32_t>(std::stoul(result[0])),
                static_cast<uint32_t>(std::stoul(result[3])),
                static_cast<uint8_t>(std::stoul(result[1])),
                static_cast<uint8_t>(std::stoul(result[2])));
        }
        else
        {
            // No data
            return ZabbixAlert(0, 0, 0, 0);
        }
    }
    uint32_t clock;
    uint32_t hostid;
    uint8_t severity : 7;
    uint8_t suppressed : 1;
    // std::string host;
    // std::string name;

    // Compare function: compares based on clock, hostid, severity,
    // and suppressed.
    bool compare(const ZabbixAlert& other) const
    {
        return clock == other.clock && hostid == other.hostid
               && severity == other.severity && suppressed == other.suppressed;
    }

    // Overload == operator for equality comparison
    bool operator==(const ZabbixAlert& other) const
    {
        return compare(other);
    }
};

/* Globals. */

using pimoroni::Point;
using pimoroni::Rect;

pimoroni::PicoGraphics_PenRGB888 graphics(32, 32, nullptr);
pimoroni::CosmicUnicorn cosmic_unicorn;

float lifetime[32][32];
float age[32][32];

State app_state;
int http_state;
uint32_t wait_until;
bool alt_colors = false;
bool a_button_prev = false;
bool game_of_life = false;
bool b_button_prev = false;
bool show_suppressed_count = true;
int bg_mode = 3;
bool d_button_prev = false;
bool c_button_prev = false;
bool gol_grid[32][32];
uint32_t gol_next_update;
uint32_t doom_face_until;
bool doom_face_enabled = true;
bool vol_up_prev = false;
bool vol_down_prev = false;
uint32_t sound_until;
int sound_repeats_remaining;
uint32_t next_beep_at;
bool whoop_active = false;
uint32_t whoop_start_ms = 0;
bool coin_active = false;
uint32_t coin_start_ms = 0;
bool initial_load = true;

/* Mario flagpole note sequence. */
static const uint16_t FLAGPOLE_FREQS[] = {392,440,494,523,587,659,740,784,784};
static const uint16_t FLAGPOLE_DUR[]   = { 60, 60, 60, 60, 60, 60, 60, 60,500};
static const int FLAGPOLE_NOTES = 9;
bool flagpole_active = false;
int  flagpole_note   = 0;
uint32_t flagpole_next_at = 0;
/* We expect updates every 15 s, so after 30 s we turn gray. */
constexpr int updates_at_least_every = 30000;
uint32_t last_update;

int boot_delay;
int watchdog_timer;
std::string trigger_url;
std::string auth_header;

int http_status;
std::string http_response;
httpclient_request_t* http_request;

std::vector<ZabbixAlert> alerts;

constexpr float hue(float hue360)
{
    return hue360 / 360.0;
}

constexpr float HUE_RED = hue(0);
constexpr float HUE_ORANGE = hue(30);
constexpr float HUE_YELLOW = hue(60);
constexpr float HUE_LIME = hue(90);
constexpr float HUE_GREEN = hue(120);
constexpr float HUE_BLUE = hue(220);
constexpr float HUE_PINK = hue(320);

/* Functions. */

std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> tokens;
    size_t spos = 0;
    size_t epos;
    std::string token;
    while ((epos = s.find(";", spos)) != std::string::npos)
    {
        tokens.push_back(s.substr(spos, epos - spos));
        spos = epos + 1;
    }
    tokens.push_back(s.substr(spos));
    return tokens;
}

uint32_t millis()
{
    return to_ms_since_boot(get_absolute_time());
}

int is_after(uint32_t until)
{
    return (int32_t)(until - millis()) < 0;
}

void start_beep(uint16_t freq, uint8_t waveform, int repeats)
{
    auto& ch = cosmic_unicorn.synth_channel(0);
    ch.waveforms  = waveform;
    ch.frequency  = freq;
    ch.volume     = 0xffff;
    ch.attack_ms  = 5;
    ch.decay_ms   = 200;
    ch.sustain    = 0;
    ch.release_ms = 10;
    ch.trigger_attack();
    cosmic_unicorn.play_synth();
    sound_repeats_remaining = repeats - 1;
    next_beep_at  = millis() + 250;
    sound_until   = millis() + 250 * repeats + 150;
}

void gol_seed()
{
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            gol_grid[x][y] = (rand() % 3) == 0;
}

void gol_step()
{
    bool next[32][32];
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx)
                    if (dx || dy)
                        n += gol_grid[(x + dx + 32) % 32][(y + dy + 32) % 32];
            next[x][y] = n == 3 || (gol_grid[x][y] && n == 2);
        }
    }
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
            gol_grid[x][y] = next[x][y];
}

void draw_xpm_image(const uint8_t* img)
{
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x)
        {
            const uint8_t* p = img + (y * 32 + x) * 3;
            graphics.set_pen(graphics.create_pen(p[0], p[1], p[2]));
            graphics.pixel(Point(x, y));
        }
}

void play_alert_sound()
{
    whoop_active = true;
    whoop_start_ms = millis();
    auto& ch = cosmic_unicorn.synth_channel(0);
    ch.waveforms  = pimoroni::Waveform::SINE;
    ch.frequency  = 300;
    ch.volume     = 0xffff;
    ch.attack_ms  = 10;
    ch.decay_ms   = 10;
    ch.sustain    = 0xffff;
    ch.release_ms = 80;
    ch.trigger_attack();
    cosmic_unicorn.play_synth();
    sound_repeats_remaining = 0;
    sound_until = millis() + 1100;
}

void play_clear_sound()
{
    whoop_active = false;
    coin_active = true;
    coin_start_ms = millis();
    auto& ch = cosmic_unicorn.synth_channel(0);
    ch.waveforms  = pimoroni::Waveform::SQUARE;
    ch.frequency  = 988;
    ch.volume     = 0x5fff;
    ch.attack_ms  = 5;
    ch.decay_ms   = 10;
    ch.sustain    = 0xffff;
    ch.release_ms = 60;
    ch.trigger_attack();
    cosmic_unicorn.play_synth();
    sound_repeats_remaining = 0;
    sound_until = millis() + 350;
}

void play_flagpole_sound()
{
    whoop_active = false;
    coin_active  = false;
    flagpole_active  = true;
    flagpole_note    = 0;
    flagpole_next_at = millis();
    auto& ch = cosmic_unicorn.synth_channel(0);
    ch.waveforms  = pimoroni::Waveform::SQUARE;
    ch.volume     = 0x7fff;
    ch.attack_ms  = 5;
    ch.decay_ms   = 20;
    ch.sustain    = 0xcfff;
    ch.release_ms = 40;
    cosmic_unicorn.play_synth();
    sound_repeats_remaining = 0;
    sound_until = millis() + 1200;
}

void update_from_config()
{
    /* This indicates the configuration has changed - handle it if required. */
    const char* value_str;
    int value;
    if ((value_str = config_get("BOOT_DELAY")) != NULL
        && (value = atoi(value_str)) >= 0)
    {
        boot_delay = value;
    }
    /* Watchdog timer. Between 0 and 8000 ms. */
    watchdog_timer = 5000;
    if ((value_str = config_get("WATCHDOG_TIMER")) != NULL
        && (value = atoi(value_str)) >= 0)
    {
        if (value > 8000)
        {
            value = 8000;
        }
        watchdog_timer = value;
    }
    if (watchdog_timer)
    {
        watchdog_enable(watchdog_timer, 0);
    }
    else
    {
        watchdog_disable();
    }
    /* Switch to potentially new WiFi credentials. */
    httpclient_set_credentials(
        config_get("WIFI_SSID"), config_get("WIFI_PASSWORD"));
    /* Switch to potentially new ZABBIX API and TOKEN. */
    trigger_url = std::string(config_get("ZABBIX_API")) + "?a=v0.1/triggers";
    auth_header =
        (std::string("Authorization: Bearer ") + config_get("ZABBIX_TOKEN")
         + "\r\n");
}

int main()
{
    /* Initialise stdio handling. */
    stdio_init_all();

    /* Initialise the WiFi chipset. */
    if (cyw43_arch_init())
    {
        printf("Failed to initialise the WiFI chipset (cyw43)\n");
        return 1;
    }

    /* And the USB handling. */
    usbfs_init();

    /* Declare some default configuration details. */
    config_t default_config[] = {
        /* NOTE: We use the BOOT_DELAY for a delay during startup. That way
         * way we can attach a serial console in time and check debug info. */
        {"BOOT_DELAY", "0"},
        /* While the HTTP code is flaky, we use the watchdog to restart.
         * This is limited to 8388 ms. We'll cap it to 8000 ms. */
        {"WATCHDOG_TIMER", "8000"},
        /* NOTE: There's no need to update these here! You can replace them
         * in CONFIG.TXT after mounting the runtime mount point (usbfs!). */
        {"WIFI_SSID", "my_network"},
        {"WIFI_PASSWORD", "my_password"},
        /* NOTE: We need a separate api_csv.php, as the api_jsonrpc.php
         * requires multiple calls.
         * NOTE: The TLS handler does not support TLS 1.3, so the side
         * needs 1.2 or lower.
         * NOTE: If the site has PFS the certificate needs to be ECDSA.
         * For RSA RSA we'd need a cipher without DHE. */
        {"ZABBIX_API", "http://zabbix.example.com/api_csv.php"},
        /* NOTE: 64 char Zabbix API token. */
        {"ZABBIX_TOKEN", "abc123"},
        {"", ""}};

    /* Set up the initial load of the configuration file. */
    config_load("config.txt", default_config, 10);

    /* Save it straight out, to preserve any defaults we put there. */
    config_save();

    /* Get initial configuration. */
    update_from_config();

    /* Init eighties super computer code. */
    for (int y = 0; y < 32; ++y)
    {
        for (int x = 0; x < 32; ++x)
        {
            lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
            age[x][y] = ((rand() % 100) / 100.0f) * lifetime[x][y];
        }
    }

    /* Init display (and serial port?). */
    cosmic_unicorn.init();

    /* Wait a bit. This sleep allows you to attach a serial console
     * (ttyACM0) to get debug info from the start. */
    if (boot_delay)
    {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1); /* enable PicoW LED */
        usbfs_sleep_ms(boot_delay);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0); /* disable PicoW LED */
    }

    /* Notify why we (re)started. */
    if (watchdog_caused_reboot())
    {
        printf("Rebooted by Watchdog!\n");
        for (int i = 0; i < 5; ++i)
        {
            for (int lightness = 1; lightness >= 0; --lightness)
            {
                graphics.set_pen(graphics.create_pen_hsv(
                    HUE_ORANGE, 1.0, (float)lightness));
                graphics.rectangle(Rect(0, 0, 32, 32));
                cosmic_unicorn.update(&graphics);
                usbfs_sleep_ms(200);
            }
        }
    }
    else
    {
        printf("Clean boot\n");
    }

    /* Last update was never. */
    last_update = millis() - updates_at_least_every;

    /* Enter the main program loop now. */
    while (true)
    {
        /* Monitor the configuration file and update vars */
        if (config_check())
        {
            update_from_config();
        }

        /* Handle state change */
        switch (app_state)
        {
        case ST_DO_REQUEST:
            /* Set up the API request. */
            app_state = ST_WAIT_RESPONSE;
            if (http_request == NULL)
            {
                http_request = httpclient_open2(
                    "GET", trigger_url.c_str(), NULL, 1024,
                    auth_header.c_str(), NULL);
            }
            else
            {
                printf("BUG: ST_DO_REQUEST: http_request non-zero?\n");
            }
            break;

        case ST_WAIT_RESPONSE:
            /* Check API response. */
            if (http_request)
            {
                httpclient_status_t new_http_state =
                    (httpclient_check(http_request));
                switch (new_http_state)
                {
                case HTTPCLIENT_NONE:
                case HTTPCLIENT_WIFI_INIT:
                case HTTPCLIENT_WIFI:
                    if (new_http_state != http_state)
                    {
                        printf(
                            "HTTPCLIENT_* state change %d -> %d"
                            "(no timeout)\n",
                            http_state, new_http_state);
                    }
                    break;
                case HTTPCLIENT_DNS:
                case HTTPCLIENT_CONNECT: // <- long TLS waits/stalls
                case HTTPCLIENT_REQUEST:
                case HTTPCLIENT_RESPONSE_STATUS:
                case HTTPCLIENT_HEADERS:
                case HTTPCLIENT_DATA:
                    if (http_state == HTTPCLIENT_DNS
                        || http_state == HTTPCLIENT_CONNECT
                        || http_state == HTTPCLIENT_REQUEST
                        || http_state == HTTPCLIENT_RESPONSE_STATUS
                        || http_state == HTTPCLIENT_HEADERS
                        || http_state == HTTPCLIENT_DATA)
                    {
                        if (is_after(wait_until))
                        {
                            printf("HTTPCLIENT_* timeout\n");
                            http_status = 408; /* TIMEOUT */
                            app_state = ST_HANDLE_RESPONSE;
                        }
                    }
                    if (new_http_state != http_state)
                    {
                        printf(
                            "HTTPCLIENT_* state change %d -> %d\n", http_state,
                            new_http_state);
                        /* Max HTTP state change timeout 15 s. */
                        wait_until = millis() + 15000;
                    }
                    break;
                case HTTPCLIENT_COMPLETE:
                case HTTPCLIENT_TRUNCATED:
                    // FIXME: do something with truncated response?
                    printf(
                        "HTTPCLIENT_COMPLETE? (%d) response code %d\n",
                        new_http_state, http_request->http_status);
                    http_status = http_request->http_status;
                    http_response = std::string(
                        httpclient_get_response(http_request),
                        http_request->response_length);
                    printf("Response: [[[%s]]]\n", http_response.c_str());
                    printf("Mem free: %lu\n", mem_heap_free());
                    httpclient_close(http_request);
                    http_request = NULL;
                    printf(
                        "Mem free: %lu (after closing http)\n",
                        mem_heap_free());
                    app_state = ST_HANDLE_RESPONSE;
                    break;
                case HTTPCLIENT_FAILED:
                    printf(
                        "HTTPCLIENT_FAILED response code %d\n",
                        http_request->http_status);
                    http_status = 0;
                    httpclient_close(http_request);
                    http_request = NULL;
                    app_state = ST_HANDLE_RESPONSE;
                    break;
                }
                http_state = new_http_state;
            }
            else
            {
                printf("BUG: ST_API_REQUEST: why is http_request zero?\n");
            }
            break;

        case ST_HANDLE_RESPONSE:
            /* Handle response. */
            printf(
                "ST_API_RESPONSE (%d): [[[%s]]]\n", http_status,
                http_response.c_str());
            if (http_status == 200)
            {
                // clock;severity;suppressed;hostid;host;name
                // 1733896822;5;0;12847;node1.example.com;CPU 25+% busy
                std::vector<ZabbixAlert> results;
                // If there are 225 rows, we have 15 * 15 blocks,
                // which is the limit that fits on our display.
                for (std::string::size_type i(0), len(http_response.length()),
                     pos(0), row(0);
                     i <= len && row <= 225; ++i)
                {
                    if (http_response[i] == '\n' || i == len)
                    {
                        if (row && (i - pos))
                        {
                            results.push_back(ZabbixAlert::from_csv(
                                http_response.substr(pos, i - pos)));
                        }
                        row++;
                        pos = i + 1;
                    }
                }
                http_response.clear();
                if (std::equal(results.begin(), results.end(), alerts.begin()))
                {
                    // no change
                    printf("No changes\n");
                }
                else
                {
                    printf("Alerts changef\n");
                }
                // Check for new unsuppressed (red) alerts.
                bool has_new_red = false;
                for (const auto& r : results)
                {
                    if (!r.suppressed
                        && !std::any_of(
                            alerts.begin(), alerts.end(),
                            [&r](const ZabbixAlert& a) { return r == a; }))
                    {
                        has_new_red = true;
                        break;
                    }
                }
                // Check for cleared unsuppressed (red) alerts.
                bool has_cleared_red = false;
                for (const auto& a : alerts)
                {
                    if (!a.suppressed
                        && !std::any_of(
                            results.begin(), results.end(),
                            [&a](const ZabbixAlert& r) { return a == r; }))
                    {
                        has_cleared_red = true;
                        break;
                    }
                }
                if (!initial_load)
                {
                    if (has_new_red)
                    {
                        if (doom_face_enabled) {
                            play_alert_sound();
                            doom_face_until = millis() + 2000;
                        }
                    }
                    else if (has_cleared_red && doom_face_enabled)
                    {
                        if (results.empty())
                            play_flagpole_sound();
                        else
                            play_clear_sound();
                    }
                }
                initial_load = false;
                // Replace old. We have no transitions yet.
                alerts = results;
                std::stable_sort(alerts.begin(), alerts.end(),
                    [](const ZabbixAlert& a, const ZabbixAlert& b) {
                        return !a.suppressed && b.suppressed;
                    });
                last_update = millis();
                app_state = ST_TRANSITION;
            }
            else
            {
                /* Sucks to be you.. */
                http_response.clear();
                app_state = ST_SLEEP;
                wait_until = millis() + 10000;
            }
            break;

        case ST_TRANSITION:
            /* Only called if we're showing a change. */
            /* NOT IMPLEMENTED YET */
            app_state = ST_SLEEP;
            wait_until = millis() + 10000;

        case ST_SLEEP:
            if (is_after(wait_until))
            {
                app_state = ST_DO_REQUEST;
            }
            break;
        }

        /* Advance flagpole note sequence. */
        if (flagpole_active && is_after(flagpole_next_at))
        {
            if (flagpole_note < FLAGPOLE_NOTES)
            {
                auto& ch = cosmic_unicorn.synth_channel(0);
                ch.frequency = FLAGPOLE_FREQS[flagpole_note];
                ch.trigger_attack();
                flagpole_next_at = millis() + FLAGPOLE_DUR[flagpole_note];
                flagpole_note++;
            }
            else
            {
                flagpole_active = false;
            }
        }

        /* Sweep frequency for whoop sound. */
        if (whoop_active)
        {
            uint32_t elapsed = millis() - whoop_start_ms;
            if (elapsed < 1000)
            {
                float t = (elapsed % 500) / 500.0f;
                float v = (t < 0.5f) ? (t * 2.0f) : ((1.0f - t) * 2.0f);
                cosmic_unicorn.synth_channel(0).frequency = (uint16_t)(300 + 600 * v);
            }
            else
            {
                whoop_active = false;
            }
        }

        /* Switch to second note of coin sound after 80ms. */
        if (coin_active)
        {
            uint32_t elapsed = millis() - coin_start_ms;
            if (elapsed >= 80 && cosmic_unicorn.synth_channel(0).frequency == 988)
                cosmic_unicorn.synth_channel(0).frequency = 1319;
            if (elapsed >= 300)
                coin_active = false;
        }

        /* Handle beep repeats and stop synth when done. */
        if (sound_repeats_remaining > 0 && is_after(next_beep_at))
        {
            cosmic_unicorn.synth_channel(0).trigger_attack();
            sound_repeats_remaining--;
            next_beep_at = millis() + 250;
        }
        if (sound_until && is_after(sound_until))
        {
            cosmic_unicorn.stop_playing();
            sound_until = 0;
            whoop_active = false;
            coin_active = false;
            flagpole_active = false;
        }

        /* Volume buttons toggle doom face. */
        bool vol_up   = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_VOLUME_UP);
        bool vol_down = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_VOLUME_DOWN);
        if (vol_up && !vol_up_prev)
            doom_face_enabled = true;
        if (vol_down && !vol_down_prev)
            doom_face_enabled = false;
        vol_up_prev   = vol_up;
        vol_down_prev = vol_down;

        /* Monitor +/- buttons. */
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_UP))
        {
            cosmic_unicorn.adjust_brightness(+0.01);
        }
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_DOWN))
        {
            cosmic_unicorn.adjust_brightness(-0.01);
        }

        /* Toggle alt color scheme on A button (rising edge). */
        bool a_button = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_A);
        if (a_button && !a_button_prev)
        {
            alt_colors = !alt_colors;
        }
        a_button_prev = a_button;

        /* Toggle Game of Life on B button (rising edge). */
        bool b_button = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_B);
        if (b_button && !b_button_prev)
        {
            game_of_life = !game_of_life;
            if (game_of_life)
            {
                gol_seed();
                gol_next_update = millis();
            }
        }
        b_button_prev = b_button;

        /* Toggle suppressed count display on C button (rising edge). */
        bool c_button = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_C);
        if (c_button && !c_button_prev)
        {
            show_suppressed_count = !show_suppressed_count;
        }
        c_button_prev = c_button;

        /* D button: cycle background animation mode. */
        bool d_button = cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_D);
        if (d_button && !d_button_prev)
            bg_mode = (bg_mode + 1) % 6;
        d_button_prev = d_button;

        graphics.set_pen(0, 0, 0);
        graphics.clear();

        /* Get info about ZabbixAlerts on display. */
        int suppressed_count = 0;
        for (const auto& a : alerts)
            if (a.suppressed) suppressed_count++;
        int active_count = (int)alerts.size() - suppressed_count;

        /* In count mode, size grid by active alerts only and leave 6 px at
         * the bottom for the number. */
        bool count_mode = show_suppressed_count && suppressed_count > 0;
        bool suppressed_as_cubes = count_mode && active_count == 0;
        int alerts_to_show = count_mode
            ? (suppressed_as_cubes ? suppressed_count : active_count)
            : (int)alerts.size();
        int available = count_mode ? 25 : 31;

        float alert_sqrt = sqrt(alerts_to_show);
        int row_col_size = static_cast<int>(std::ceil(alert_sqrt));
        if (row_col_size <= 1)
        {
            row_col_size = 2;
        }
        int block_size = available / row_col_size;
        /* Offset: when showing 9 alerts we want 1 pixel on all 4 sides,
         * not 2 left and 2 below. */
        int block_offset = (available - (row_col_size * block_size)) / 2 + 1;

        /* Lightness depends on wifi/connection state. */
        bool has_recent_data =
            ((millis() - last_update) < updates_at_least_every);
        float saturation = has_recent_data ? 1.0 : 0.0;
        float lightness = has_recent_data ? 0.6 : 0.3;

        float bg_hue = alt_colors ? HUE_BLUE : HUE_LIME;

        if (doom_face_enabled && doom_face_until && !is_after(doom_face_until))
        {
            draw_xpm_image(DOOM_ALERT);
        }
        else if (doom_face_enabled && alerts.empty() && !game_of_life)
        {
            draw_xpm_image(DOOM_RESOLVED);
        }
        else if (game_of_life)
        {
            /* Step the Game of Life at ~150 ms per generation. */
            if (is_after(gol_next_update))
            {
                gol_step();
                gol_next_update = millis() + 150;
            }
            for (int y = 0; y < 32; ++y)
            {
                for (int x = 0; x < 32; ++x)
                {
                    if (gol_grid[x][y])
                    {
                        graphics.set_pen(graphics.create_pen_hsv(
                            bg_hue, 1.0f, 1.0f));
                        graphics.pixel(Point(x, y));
                    }
                }
            }
        }
        else
        {

        /* Background animation (bg_mode 0-3). */
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                float a = age[x][y], l = lifetime[x][y];
                float step = 0.01f;

                switch (bg_mode)
                {
                case 0: /* Rain: sparse decaying flashes. */
                    if (a < l * 0.3f)
                        graphics.set_pen(graphics.create_pen_hsv(
                            bg_hue, saturation, lightness));
                    else if (a < l * 0.5f)
                        graphics.set_pen(graphics.create_pen_hsv(
                            bg_hue, saturation,
                            lightness * (l * 0.5f - a) * 5.0f));
                    else
                        goto next_pixel;
                    break;

                case 1: /* Twinkle: brief bright sparks, no fade. */
                    step = 0.025f;
                    if (a < l * 0.12f)
                        graphics.set_pen(graphics.create_pen_hsv(
                            bg_hue, saturation, lightness));
                    else
                        goto next_pixel;
                    break;

                case 2: /* Pulse: smooth sine breathing. */
                    step = 0.006f;
                    {
                        float v = sinf(a / l * 3.14159f);
                        if (v > 0.0f)
                            graphics.set_pen(graphics.create_pen_hsv(
                                bg_hue, saturation, lightness * v));
                        else
                            goto next_pixel;
                    }
                    break;

                case 3: /* Sweep: glowing diagonal front, top-left to bottom-right. */
                    {
                        float sweep = fmodf(millis() * 0.04f, 80.0f) - 8.0f;
                        float dist  = sweep - (x + y);
                        if (dist >= 0.0f && dist < 6.0f)
                            graphics.set_pen(graphics.create_pen_hsv(
                                bg_hue, saturation,
                                lightness * (1.0f - dist / 6.0f)));
                        else
                            goto next_pixel;
                    }
                    break;

                case 5: /* Sweep fast: multiple rapid diagonal fronts. */
                    {
                        float pos  = fmodf(millis() * 0.04f, 30.0f);
                        float dist = fmodf(pos - (x + y) * 30.0f / 64.0f + 30.0f, 30.0f);
                        if (dist < 5.0f)
                            graphics.set_pen(graphics.create_pen_hsv(
                                bg_hue, saturation,
                                lightness * (1.0f - dist / 5.0f)));
                        else
                            goto next_pixel;
                    }
                    break;

                case 4: /* Wave: horizontal sine band sweeping down. */
                    step = 0.007f;
                    {
                        float phase = a / l * 6.28318f;
                        float wave  = 0.5f + 0.5f * sinf(phase + x * 0.4f);
                        float v = lightness * wave;
                        if (v > 0.05f)
                            graphics.set_pen(graphics.create_pen_hsv(
                                bg_hue, saturation, v));
                        else
                            goto next_pixel;
                    }
                    break;
                }
                graphics.pixel(Point(x, y));

                next_pixel:
                if (a + step >= l)
                {
                    age[x][y]     = 0.0f;
                    lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
                }
                else
                {
                    age[x][y] += step;
                }
            }
        }

        /* Write animated rectangles for all alerts. */
        size_t alert_idx = 0;
        for (int y = block_offset; y < block_size * row_col_size;
             y += block_size)
        {
            for (int x = block_offset; x < block_size * row_col_size;
                 x += block_size)
            {
                if (alert_idx < alerts.size())
                {
                    bool is_suppressed = alerts[alert_idx].suppressed;

                    if (show_suppressed_count && is_suppressed && !suppressed_as_cubes)
                    {
                        alert_idx += 1;
                        continue;
                    }

                    float base_saturation = has_recent_data ? 1.0f : 0.5f;
                    float base_lightness = has_recent_data ? 1.0f : 0.6f;
                    float alert_hue = HUE_RED;
                    if (is_suppressed)
                    {
                        if (alt_colors)
                        {
                            alert_hue     = HUE_PINK;
                            base_saturation = 1.0f;
                            base_lightness  = 0.6f;
                        }
                        else
                        {
                            base_saturation = 0.0f;
                            base_lightness  = 0.6f;
                        }
                    }
                    else if (alt_colors)
                    {
                        base_lightness *= 0.45f;
                    }

                    for (int w = x; w < x + block_size - 1; ++w)
                    {
                        for (int h = y; h < y + block_size - 1; ++h)
                        {
                            if (age[w][h] < lifetime[w][h] * 0.3f)
                            {
                                graphics.set_pen(graphics.create_pen_hsv(
                                    alert_hue, base_saturation, base_lightness));
                            }
                            else if (age[w][h] < lifetime[w][h] * 0.5f)
                            {
                                float decay =
                                    (lifetime[w][h] * 0.5f - age[w][h])
                                    * 5.0f / lifetime[w][h];
                                graphics.set_pen(graphics.create_pen_hsv(
                                    alert_hue, base_saturation,
                                    base_lightness * decay));
                            }
                            else
                            {
                                graphics.set_pen(graphics.create_pen_hsv(
                                    alert_hue, base_saturation, base_lightness * 0.5f));
                            }
                            graphics.pixel(Point(w, h));
                        }
                    }

                    alert_idx += 1;
                }
            }
        }
        /* Display suppressed alert count in bottom-right corner. */
        if (count_mode && !game_of_life)
        {
            {
                std::string count_str = std::to_string(suppressed_count);
                graphics.set_font(&font8);
                int32_t text_w = graphics.measure_text(count_str, 1.0f, 1);
                int tx = 32 - text_w;
                int ty = 25;
                graphics.set_pen(0, 0, 0);
                graphics.rectangle(Rect(tx - 1, ty - 1, text_w + 1, 9));
                if (alt_colors)
                    graphics.set_pen(graphics.create_pen_hsv(HUE_PINK, 1.0f, 0.8f));
                else
                    graphics.set_pen(255, 255, 255);
                graphics.text(count_str, Point(tx, ty), 32, 1.0f, 0.0f, 1);
            }
        }
        } /* end else (normal display) */

        /* Connection health pixel at (0,0) — blinks at 1 Hz. */
        if ((millis() / 500) % 2 == 0)
        {
            uint32_t age_ms = millis() - last_update;
            if (age_ms < (uint32_t)updates_at_least_every)
                graphics.set_pen(graphics.create_pen_hsv(HUE_GREEN, 1.0f, 0.8f));
            else if (age_ms < 120000)
                graphics.set_pen(graphics.create_pen_hsv(HUE_ORANGE, 1.0f, 0.8f));
            else
                graphics.set_pen(graphics.create_pen_hsv(HUE_RED, 1.0f, 0.8f));
            graphics.pixel(Point(0, 0));
        }

        /* Mute indicator at (1,0): blue when sound/face suppressed, else background. */
        if (!doom_face_enabled)
        {
            graphics.set_pen(graphics.create_pen_hsv(HUE_BLUE, 1.0f, 0.8f));
            graphics.pixel(Point(1, 0));
        }

        /* Update display and sleep a bit. */
        cosmic_unicorn.update(&graphics);
        usbfs_sleep_ms(10); /* instead of sleep_ms(10); */

        /* Update watchdog. */
        watchdog_update();
    }

    /* We never get here. */
    __builtin_unreachable();
    return 0;
}

/* vim: set ts=8 sw=4 sts=4 et ai: */
