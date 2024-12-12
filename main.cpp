/*
 * main.cpp - part of PIM670 Zabbix Display
 */

/* Standard header files. */

#include <stdio.h>
#include <stdlib.h>


/* SDK header files. */

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"


/* Local header files. */

#include "usbfs.h"
#include "opt/config.h"
#include "opt/httpclient.h"
#include "opt/internals.h"


/* Stuff from pimoroni example */

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "cosmic_unicorn.hpp"


/* Our stuff. */

#include <string>


/* Globals. */

typedef enum States {
    ST_DO_REQUEST = 0,
    ST_WAIT_RESPONSE,
    ST_HANDLE_RESPONSE,
    ST_TRANSITION,
    ST_SLEEP
} State;

pimoroni::PicoGraphics_PenRGB888 graphics(32, 32, nullptr);
pimoroni::CosmicUnicorn cosmic_unicorn;

float lifetime[32][32];
float age[32][32];

State app_state;
int http_state;
uint32_t wait_until;

int blink_rate = 250;
std::string trigger_url;
std::string auth_header;
int http_status;
std::string http_response;
httpclient_request_t *http_request;

class ZabbixAlert {
    public:
        static ZabbixAlert from_csv(const std::string& s) {
            /* Not important yet. First when we add "change" transitions
             * does this become important. */
            return ZabbixAlert();
        }
        uint32_t clock;
        uint32_t hostid;
        uint8_t severity:1;
        uint8_t suppressed:1;
        //std::string host;
        //std::string name;
};

std::vector<ZabbixAlert> alerts;


/* Functions. */

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
    if ((value_str = config_get("BLINK_RATE")) != NULL &&
            (value = atoi(value_str)) > 0)
    {
        blink_rate = value;
    }
    /* Switch to potentially new WiFi credentials. */
    httpclient_set_credentials(config_get("WIFI_SSID"), config_get("WIFI_PASSWORD"));
    /* Switch to potentially new ZABBIX API and TOKEN. */
    trigger_url = std::string(config_get("ZABBIX_API" )) + "?a=v0.1/triggers";
    auth_header = std::string("Authorization: Bearer ") + config_get("ZABBIX_TOKEN") + "\r\n";
}


int main()
{
    /* Initialise stdio handling. */
    stdio_init_all();

    /* Initialise the WiFi chipset. */
    if (cyw43_arch_init())
    {
        printf( "Failed to initialise the WiFI chipset (cyw43)\n" );
        return 1;
    }

    /* And the USB handling. */
    usbfs_init();

    /* Declare some default configuration details. */
    config_t default_config[] = {
        /* NOTE: We (ab)use the BLINK_RATE for a delay during startup. That way
         * way we can attach a serial console in time and check debug info. */
        {"BLINK_RATE", "3000"},
        /* NOTE: There's no need to update these here! You can replace them
         * in CONFIG.TXT after mounting the runtime mount point (usbfs!). */
        {"WIFI_SSID", "my_network"},
        {"WIFI_PASSWORD", "my_password"},
        /* NOTE: We need a separate api_csv.php, as the api_jsonrpc.php requires
         * multiple calls.
         * NOTE: The TLS handler does not support TLS 1.3, so the side needs 1.2
         * or lower.
         * NOTE: If the site has PFS the certificate needs to be ECDSA. For RSA
         * RSA we'd need a cipher without DHE. */
        {"ZABBIX_API", "http://zabbix.example.com/api_csv.php"},
        /* NOTE: 64 char Zabbix API token. */
        {"ZABBIX_TOKEN", "abc123"},
        {"", ""}
    };

    /* Set up the initial load of the configuration file. */
    config_load("config.txt", default_config, 10);

    /* Save it straight out, to preserve any defaults we put there. */
    config_save();

    /* Get initial configuration. */
    update_from_config();

    /* Init eighties super computer code. */
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 32; ++x) {
            lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
            age[x][y] = ((rand() % 100) / 100.0f) * lifetime[x][y];
        }
    }

    /* Init display (and serial port?). */
    cosmic_unicorn.init();

    /* Wait a bit. This sleep allows you to attach a serial console
     * (ttyACM0) to get debug info from the start. */
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);  /* enable PicoW LED */
    usbfs_sleep_ms(blink_rate);
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);  /* disable PicoW LED */

    /* Enter the main program loop now. */
    while (true)
    {
        /* Monitor the configuration file and update vars */
        if (config_check())
        {
            update_from_config();
        }

        /* Handle state change */
        switch (app_state) {
            case ST_DO_REQUEST:
                /* Set up the API request. */
                app_state = ST_WAIT_RESPONSE;
                if (http_request == NULL)
                {
                    http_request = httpclient_open2("GET", trigger_url.c_str(), NULL, 1024, auth_header.c_str(), NULL);
                }
                else
                {
                    printf("BUG: ST_DO_REQUEST: why is http_request non-zero?\n");
                }
                break;

            case ST_WAIT_RESPONSE:
                /* Check API response. */
                if (http_request)
                {
                    httpclient_status_t new_http_state = httpclient_check(http_request);
                    switch (new_http_state) {
                        case HTTPCLIENT_NONE:
                        case HTTPCLIENT_WIFI_INIT:
                        case HTTPCLIENT_WIFI:
                            if (new_http_state != http_state)
                            {
                                printf("HTTPCLIENT_* state change %d -> %d (no timeout)\n", http_state, new_http_state);
                            }
                            break;
                        case HTTPCLIENT_DNS:
                        case HTTPCLIENT_CONNECT: /* blame TLS for long waits/stall here! */
                        case HTTPCLIENT_REQUEST:
                        case HTTPCLIENT_RESPONSE_STATUS:
                        case HTTPCLIENT_HEADERS:
                        case HTTPCLIENT_DATA:
                            if (http_state == HTTPCLIENT_DNS ||
                                    http_state == HTTPCLIENT_CONNECT ||
                                    http_state == HTTPCLIENT_REQUEST ||
                                    http_state == HTTPCLIENT_RESPONSE_STATUS ||
                                    http_state == HTTPCLIENT_HEADERS ||
                                    http_state == HTTPCLIENT_DATA)
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
                                printf("HTTPCLIENT_* state change %d -> %d\n", http_state, new_http_state);
                                wait_until = millis() + 15000; /* set max http timeout to 15s */
                            }
                            break;
                        case HTTPCLIENT_COMPLETE:
                            printf("HTTPCLIENT_COMPLETE response code %d\n", http_request->http_status);
                            http_status = http_request->http_status;
                            http_response = std::string(httpclient_get_response(http_request), http_request->response_length);
                            printf("Response: [[[%s]]]\n", http_response.c_str());
                            printf("Mem free: %lu\n", mem_heap_free());
                            httpclient_close(http_request);
                            http_request = NULL;
                            printf("Mem free: %lu (after closing http)\n", mem_heap_free());
                            app_state = ST_HANDLE_RESPONSE;
                            break;
                        case HTTPCLIENT_TRUNCATED:
                            printf("HTTPCLIENT_TRUNCATED response code %d\n", http_request->http_status);
                            http_status = 499;
                            httpclient_close(http_request);
                            http_request = NULL;
                            app_state = ST_HANDLE_RESPONSE;
                        case HTTPCLIENT_FAILED:
                            printf("HTTPCLIENT_FAILED response code %d\n", http_request->http_status);
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
                printf("ST_API_RESPONSE (%d): [[[%s]]]\n", http_status, http_response.c_str());
                if (http_status == 200)
                {
                    // clock;severity;suppressed;hostid;host;name
                    // 1733896822;5;0;12847;node1.example.com;CPU 25+% busy with I/O for >1h on node
                    std::vector<ZabbixAlert> results;
                    for (std::string::size_type i(0), len(http_response.length()), pos(0), row(0); i <= len; i++) {
                        if (http_response[i] == '\n' || i == len) {
                            if (row && (i - pos)) {
                                results.push_back(ZabbixAlert::from_csv(http_response.substr(pos, i - pos)));
                            }
                            row++;
                            pos = i + 1;
                        }
                    }
                    http_response.clear();
                    // wiping.. no transitions yet..
                    alerts = results;
                    //alerts.assign(results.begin(), results.end());
                    app_state = ST_TRANSITION;
                }
                else
                {
                    /* sucks to be you.. */
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
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_UP)) {
            cosmic_unicorn.adjust_brightness(+0.01);
        }
        if (cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_DOWN )) {
            cosmic_unicorn.adjust_brightness(-0.01);
        }

        graphics.set_pen(0, 0, 0);
        graphics.clear();

        /* Update eighties super computer. */
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                if (age[x][y] < lifetime[x][y] * 0.3f) {
                    graphics.set_pen(230, 150, 0);
                    graphics.pixel(pimoroni::Point(x, y));
                } else if(age[x][y] < lifetime[x][y] * 0.5f) {
                    float decay = (lifetime[x][y] * 0.5f - age[x][y]) * 5.0f;
                    graphics.set_pen(decay * 230, decay * 150, 0);
                    graphics.pixel(pimoroni::Point(x, y));
                }
                if (age[x][y] >= lifetime[x][y]) {
                    age[x][y] = 0.0f;
                    lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
                }
                age[x][y] += 0.01f;
            }
        }

        /* Update ZabbixAlerts on display. */
        const int block_size = 16;
        int alerts_to_show = alerts.size();
        for (int x = 0; x < 32; x += block_size) {
            for (int y = 0; y < 32; y += block_size) {
                if (alerts_to_show) {
                    int w;
                    for (w = x; w < x + block_size - 1; ++w) {
                        graphics.set_pen(255, 0, 0);
                        for (int h = y; h < y + block_size - 1; ++h) {
                            graphics.pixel(pimoroni::Point(w, h));
                        }
                        graphics.set_pen(64, 0, 0);
                        graphics.pixel(pimoroni::Point(w, y + block_size - 1));
                    }
                    for (int h = y; h < y + block_size; ++h) {
                        graphics.pixel(pimoroni::Point(w, h));
                    }
                    alerts_to_show -= 1;
                }
            }
        }

        /* Update display and sleep a bit. */
        cosmic_unicorn.update(&graphics);
        usbfs_sleep_ms(10);  /* instead of sleep_ms(10); */
    }

    /* We never get here. */
    __builtin_unreachable();
    return 0;
}

/* vim: set ts=8 sw=4 sts=4 et ai: */
