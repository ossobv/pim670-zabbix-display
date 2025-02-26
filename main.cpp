/*
 * main.cpp - part of PIM670 Zabbix Display
 */

/* Standard header files. */

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
                // Replace old. We have no transitions yet.
                alerts = results;
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

        /* Monitor +/- buttons. */
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_UP))
        {
            cosmic_unicorn.adjust_brightness(+0.01);
        }
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_DOWN))
        {
            cosmic_unicorn.adjust_brightness(-0.01);
        }

        graphics.set_pen(0, 0, 0);
        graphics.clear();

        /* Get info about ZabbixAlerts on display. */
        int alerts_to_show = alerts.size();
        float alert_sqrt = sqrt(alerts_to_show);
        int row_col_size = static_cast<int>(std::ceil(alert_sqrt));
        if (row_col_size <= 1)
        {
            row_col_size = 2;
        }
        int block_size = 31 / row_col_size;
        /* Offset: when showing 9 alerts we want 1 pixel on all 4 sides,
         * not 2 left and 2 below. */
        int block_offset = (31 - (row_col_size * block_size)) / 2 + 1;

        /* Lightness depends on wifi/connection state. */
        bool has_recent_data =
            ((millis() - last_update) < updates_at_least_every);
        float saturation = has_recent_data ? 1.0 : 0.0;
        float lightness = has_recent_data ? 0.6 : 0.3;

        /* Update eighties super computer. */
        for (int y = 0; y < 32; ++y)
        {
            for (int x = 0; x < 32; ++x)
            {
                if (age[x][y] < lifetime[x][y] * 0.3f)
                {
                    graphics.set_pen(graphics.create_pen_hsv(
                        HUE_LIME, saturation, lightness));
                    graphics.pixel(Point(x, y));
                }
                else if (age[x][y] < lifetime[x][y] * 0.5f)
                {
                    float decay = (lifetime[x][y] * 0.5f - age[x][y]) * 5.0f;
                    graphics.set_pen(graphics.create_pen_hsv(
                        HUE_LIME, saturation, lightness * decay));
                    graphics.pixel(Point(x, y));
                }
                if (age[x][y] >= lifetime[x][y])
                {
                    age[x][y] = 0.0f;
                    lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
                }
                age[x][y] += 0.01f;
            }
        }

        /* Write static rectangles for all alerts. */
        size_t alert_idx = 0;
        for (int y = block_offset; y < block_size * row_col_size;
             y += block_size)
        {
            for (int x = block_offset; x < block_size * row_col_size;
                 x += block_size)
            {
                if (alert_idx < alerts.size())
                {
                    saturation = has_recent_data ? 1.0 : 0.5;
                    lightness = has_recent_data ? 1.0 : 0.6;
                    if (alerts[alert_idx].suppressed)
                    {
                        saturation = 0;
                        lightness = 0.6;
                    }
                    graphics.set_pen(graphics.create_pen_hsv(
                        HUE_RED, saturation, lightness));

                    int w;
                    for (w = x; w < x + block_size - 1; ++w)
                    {
                        for (int h = y; h < y + block_size - 1; ++h)
                        {
                            graphics.pixel(Point(w, h));
                        }
                    }

                    alert_idx += 1;
                }
            }
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
