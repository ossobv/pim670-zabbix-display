/*
 * main.cpp - part of the PicoW C/C++ Boilerplate Project
 *
 * This file defines the main() function, the entrypoint for your program.
 *
 * After the general setup, it simply blinks the LED on your PicoW as the
 * traditional 'Hello World' of the Pico!
 *
 * Copyright (C) 2023 Pete Favelle <ahnlak@ahnlak.com>
 * This file is released under the BSD 3-Clause License; see LICENSE for details.
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

/* Stuff from pimoroni example */

#include "libraries/pico_graphics/pico_graphics.hpp"
#include "cosmic_unicorn.hpp"

/* Globals. */

//using namespace pimoroni;

pimoroni::PicoGraphics_PenRGB888 graphics(32, 32, nullptr);
pimoroni::CosmicUnicorn cosmic_unicorn;

float lifetime[32][32];
float age[32][32];

/* Functions. */

/*
 * main() - the entrypoint of the application; this is what runs when the PicoW
 *          starts up, and would never normally exit.
 */

int main()
{
  int blink_rate = 250;
  httpclient_request_t *http_request;


  /* Initialise stdio handling. */
  stdio_init_all();

  /* Initialise the WiFi chipset. */
  if ( cyw43_arch_init() )
  {
    printf( "Failed to initialise the WiFI chipset (cyw43)\n" );
    return 1;
  }

  /* And the USB handling. */
  usbfs_init();

  /* Declare some default configuration details. */
  config_t default_config[] = 
  {
    { "BLINK_RATE", "250" },
    /* NOTE: There's no need to update these here! You can replace them
     * in CONFIG.TXT after mounting the runtime mount point (usbfs!). */
    { "WIFI_SSID", "my_network" },
    { "WIFI_PASSWORD", "my_password" },
    { "", "" }
  };

  /* Set up the initial load of the configuration file. */
  config_load( "config.txt", default_config, 10 );

  /* Save it straight out, to preserve any defaults we put there. */
  config_save();

  /* See if we have a blink rate in there. */
  const char *blink_rate_string = config_get( "BLINK_RATE" );
  if ( ( blink_rate_string != NULL ) &&
       ( atoi( blink_rate_string ) > 0 ) )
  {
    blink_rate = atoi( blink_rate_string );
  }

  /* Init eighties super computer code. */

  for(int y = 0; y < 32; y++) {
    for(int x = 0; x < 32; x++) {
      lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
      age[x][y] = ((rand() % 100) / 100.0f) * lifetime[x][y];
    }
  }

  cosmic_unicorn.init();

  /* Set up a simple web request. */
  httpclient_set_credentials( config_get( "WIFI_SSID" ), config_get( "WIFI_PASSWORD" ) );
  /* BEWARE: TLS-1.3+ might not work */
  http_request = httpclient_open( "https://httpbin.org/get", NULL, 1024 );

  /* Enter the main program loop now. */
  while( true )
  {
    /* Monitor the configuration file. */
    if ( config_check() )
    {
      /* This indicates the configuration has changed - handle it if required. */
      blink_rate_string = config_get( "BLINK_RATE" );
      if ( ( blink_rate_string != NULL ) &&
           ( atoi( blink_rate_string ) > 0 ) )
      {
        blink_rate = atoi( blink_rate_string );
      }

      /* Switch to potentially new WiFi credentials. */
      httpclient_set_credentials( config_get( "WIFI_SSID" ), config_get( "WIFI_PASSWORD" ) );
    }

    /* Service the http request, if still active. */
    if ( http_request != NULL )
    {
      httpclient_status_t l_status = httpclient_check( http_request );
      if ( l_status == HTTPCLIENT_COMPLETE )
      {
        printf( "HTTP response code %d\n", http_request->http_status );
        printf( "Response:\n%s\n", httpclient_get_response( http_request ) );
        httpclient_close( http_request );
        http_request = NULL;
      }
    }

    /* The blink is very simple, just toggle the GPIO pin high and low. */
    // printf( "Blinking at a rate of %d ms\n", blink_rate );
    /*
    cyw43_arch_gpio_put( CYW43_WL_GPIO_LED_PIN, 1 );
    usbfs_sleep_ms( blink_rate );

    cyw43_arch_gpio_put( CYW43_WL_GPIO_LED_PIN, 0 );
    usbfs_sleep_ms( blink_rate );
    */
    
    if(cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_UP)) {
      cosmic_unicorn.adjust_brightness(+0.01);
    }
    if(cosmic_unicorn.is_pressed(cosmic_unicorn.SWITCH_BRIGHTNESS_DOWN)) {
      cosmic_unicorn.adjust_brightness(-0.01);
    }

    graphics.set_pen(0, 0, 0);
    graphics.clear();

    for(int y = 0; y < 32; y++) {
      for(int x = 0; x < 32; x++) {
        if(age[x][y] < lifetime[x][y] * 0.3f) {
          graphics.set_pen(230, 150, 0);
          graphics.pixel(pimoroni::Point(x, y));
        }else if(age[x][y] < lifetime[x][y] * 0.5f) {
          float decay = (lifetime[x][y] * 0.5f - age[x][y]) * 5.0f;
          graphics.set_pen(decay * 230, decay * 150, 0);
          graphics.pixel(pimoroni::Point(x, y));
        }

        if(age[x][y] >= lifetime[x][y]) {
          age[x][y] = 0.0f;
          lifetime[x][y] = 1.0f + ((rand() % 10) / 100.0f);
        }

        age[x][y] += 0.01f;
      }
    }

    cosmic_unicorn.update(&graphics);

    //sleep_ms(10);
    usbfs_sleep_ms(10);
  }

  /* We would never expect to reach an end....! */
  return 0;
}

/* End of file main.cpp */
