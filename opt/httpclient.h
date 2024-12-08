/*
 * opt/httpclient.h - part of the PicoW C/C++ Boilerplate Project
 *
 * Header for an optional, simple HTTP Client, supporting HTTPS too.
 * This is provided as part of the boilerplate, but if you don't require it
 * (or have built your own) you can safely delete this file (and httpclient.c)
 * and remove it from your CMakeLists.txt file.
 * 
 * Copyright (C) 2023 Pete Favelle <ahnlak@ahnlak.com>
 * This file is released under the BSD 3-Clause License; see LICENSE for details.
 */

#pragma once


/* Constants. */

#define PCBP_HTTP_SSID_MAXLEN       32
#define PCBP_HTTP_PASSWORD_MAXLEN   64
#define PCBP_HTTP_WIFI_RETRY_MS     5000
#define PCBP_HTTP_HOST_MAXLEN       63
#define PCBP_HTTP_PATH_MAXLEN       127
#define PCBP_HTTP_TIMEOUT_SECS      10

#define PCBP_HEADER_BUFSIZE         1023
#define PCBP_REQUEST_USER_AGENT     "pim670-zabbix-display"


/* Enumerations. */

typedef enum
{
  HTTPCLIENT_NONE,
  HTTPCLIENT_WIFI_INIT,
  HTTPCLIENT_WIFI,
  HTTPCLIENT_DNS,
  HTTPCLIENT_CONNECT,
  HTTPCLIENT_REQUEST,
  HTTPCLIENT_RESPONSE_STATUS,
  HTTPCLIENT_HEADERS,
  HTTPCLIENT_DATA,
  HTTPCLIENT_TRUNCATED,
  HTTPCLIENT_COMPLETE,
  HTTPCLIENT_FAILED
} httpclient_status_t;


/* Structures */

typedef struct
{
  /* Elements used for high level request management. */
  char                  host[PCBP_HTTP_HOST_MAXLEN+1];
  char                  path[PCBP_HTTP_PATH_MAXLEN+1];
  ip_addr_t             host_addr;
  uint16_t              port;
  bool                  tls;
  httpclient_status_t   status;
  char                 *response;
  bool                  response_allocated;
  uint16_t              response_max_size;
  uint16_t              response_length;
  absolute_time_t       wifi_retry_time;

  /* Managing the HTTP request/response. */
  char*                 send_buffer;
  uint16_t              send_buffer_len;
  char                  header_buffer[PCBP_HEADER_BUFSIZE+1];
  uint16_t              http_status;
  uint16_t              content_length;

  /* Elements used for low level lwIP conversations. */
  struct altcp_pcb     *pcb;

} httpclient_request_t;


/* Function prototypes. */

#ifdef __cplusplus
extern "C" {
#endif

void                  httpclient_set_credentials( const char *, const char * );
httpclient_request_t *httpclient_open2( const char *p_method, const char *p_url,
                                        char *p_buffer, uint16_t p_buffer_size,
                                        const char *p_extra_headers,
				        const char *p_data );
inline httpclient_request_t *httpclient_open( const char *p_url,
                                              char *p_buffer,
                                              uint16_t p_buffer_size) {
    return httpclient_open2( "GET", p_url, p_buffer, p_buffer_size, "", "" );
}
httpclient_status_t   httpclient_check( httpclient_request_t * );
const char           *httpclient_get_response( const httpclient_request_t * );
void                  httpclient_close( httpclient_request_t * );

#ifdef __cplusplus
}
#endif


/* End of file opt/httpclient.h */
