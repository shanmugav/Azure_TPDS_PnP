/*
\file   wifi_service.c

\brief  Wifi service source file.

(c) 2018 Microchip Technology Inc. and its subsidiaries.

Subject to your compliance with these terms, you may use Microchip software and any
derivatives exclusively with Microchip products. It is your responsibility to comply with third party
license terms applicable to your use of third party software (including open source software) that
may accompany Microchip software.

THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY
IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS
FOR A PARTICULAR PURPOSE.

IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP
HAS BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO
THE FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL
CLAIMS IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT
OF FEES, IF ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS
SOFTWARE.
*/

#include <time.h>
#include <string.h>
#include <stdio.h>

#include "m2m_wifi.h"
#include "wifi_service.h"
#include "app.h"
#include "debug_print.h"
#include "../../../iot_config/IoT_Sensor_Node_config.h"
#include "../../../iot_config/mqtt_config.h"
#include "socket.h"
#include "../../../credentials_storage/credentials_storage.h"
#include "led.h"

#define CLOUD_WIFI_TASK_INTERVAL       50L
#define CLOUD_NTP_TASK_INTERVAL        500L
#define SOFT_AP_CONNECT_RETRY_INTERVAL 1000L

#define CFG_WLAN_AP_NAME "Naruto"
#define CFG_WLAN_AP_IP_ADDRESS \
    {                          \
        192, 168, 1, 1         \
    }

SYS_TIME_HANDLE ntpTimeFetchTaskHandle  = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE wifiHandlerTaskHandle   = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE softApConnectTaskHandle = SYS_TIME_HANDLE_INVALID;
SYS_TIME_HANDLE checkBackTaskHandle     = SYS_TIME_HANDLE_INVALID;

volatile bool ntpTimeFetchTaskTmrExpired  = false;
volatile bool wifiHandlerTaskTmrExpired   = false;
volatile bool softApConnectTaskTmrExpired = false;
volatile bool checkBackTaskTmrExpired     = false;

// Scheduler
void ntpTimeFetchTask(void);
void wifiHandlerTask(void);
void softApConnectTask(void);
void checkBackTask(void);

static bool responseFromProvisionConnect = false;

void (*callback_funcPtr)(uint8_t);
void enable_provision_ap(void);

// Callback function pointer for indicating status updates upwards
void (*wifiConnectionStateChangedCallback)(uint8_t status) = NULL;

// This is a workaround to wifi_deinit being broken in the winc, so we can de-init without hanging up
int8_t hif_deinit(void* arg);

void ntpTimeFetchTaskcb(uintptr_t context)
{
    ntpTimeFetchTaskTmrExpired = true;
}

void wifiHandlerTaskcb(uintptr_t context)
{
    wifiHandlerTaskTmrExpired = true;
}

void softApConnectTaskcb(uintptr_t context)
{
    softApConnectTaskTmrExpired = true;
}

void checkBackTaskcb(uintptr_t context)
{
    checkBackTaskTmrExpired = true;
}

// funcPtr passed in here will be called indicating AP state changes with the following values
// Wi-Fi state is disconnected   == 0
// Wi-Fi state is connected      == 1
// Wi-Fi state is undefined      == 0xff
void wifi_init(void (*funcPtr)(uint8_t), uint8_t mode)
{
    wifiConnectionStateChangedCallback = funcPtr;

    // Mode == 0 means AP configuration mode
    if (mode == WIFI_SOFT_AP)
    {
        enable_provision_ap();
        debug_printInfo("ACCESS POINT MODE for provisioning");
    }
    else
    {
        ntpTimeFetchTaskHandle = SYS_TIME_CallbackRegisterMS(ntpTimeFetchTaskcb, 0, CLOUD_NTP_TASK_INTERVAL, SYS_TIME_PERIODIC);
    }
}

bool wifi_connectToAp(uint8_t passed_wifi_creds)
{
    int8_t e = M2M_SUCCESS;

    if (passed_wifi_creds == NEW_CREDENTIALS)
    {
        e = m2m_wifi_connect((char*)ssid, sizeof(ssid), atoi((char*)authType), (char*)pass, M2M_WIFI_CH_ALL);
    }
    else if (shared_networking_params.haveAPConnection == 0)
    {
        e = m2m_wifi_default_connect();
    }

    if (M2M_SUCCESS != e)
    {
        debug_printError("WIFI: wifi error = %d", e);
        shared_networking_params.haveERROR = 1;
        return false;
    }

    return true;
}

bool wifi_disconnectFromAp(void)
{
    int8_t m2mDisconnectError;

    debug_printInfo(" WIFI: disconnect from AP");

    if (shared_networking_params.haveAPConnection == 1)
    {
        if (M2M_SUCCESS != (m2mDisconnectError = m2m_wifi_disconnect()))
        {
            debug_printError("WIFI: Disconnect from AP error = %d", m2mDisconnectError);
            return false;
        }
        //shared_networking_params.haveAPConnection = 0;
    }
    return true;
}

void softApConnectTask(void)
{
    if (!wifi_connectToAp((uint8_t)NEW_CREDENTIALS))
    {
        debug_printError("WIFI: Soft AP Connect Failure");
    }
    else
    {
        debug_printInfo("SOFT AP: New Connect credentials sent WINC");
    }
}

// Update the system time every CLOUD_NTP_TASK_INTERVAL milliseconds
void ntpTimeFetchTask(void)
{
    m2m_wifi_get_system_time();
}

void wifiHandlerTask(void)
{
    m2m_wifi_handle_events();
}

void checkBackTask(void)
{
    // debug_printWarn(" WIFI: Connection Status: %d", shared_networking_params.haveAPConnection);
    shared_networking_params.haveAPConnection = 0;
    shared_networking_params.haveERROR        = 1;
    shared_networking_params.amDisconnecting  = 0;
}

void enable_provision_ap(void)
{

    tstrM2MAPModeConfig apExtConfig;

    tstrM2MAPConfig apConfig = {
        CFG_WLAN_AP_NAME,         // Access Point Name.
        1,                        // Channel to use.
        0,                        // Wep key index.
        WEP_40_KEY_STRING_SIZE,   // Wep key size.
        "2ri4-yowp-ied7",             // Wep key.
        M2M_WIFI_SEC_WPA_PSK,        // Security mode.
        SSID_MODE_VISIBLE,        // SSID visible.
        CFG_WLAN_AP_IP_ADDRESS};

    apExtConfig.strApConfig = apConfig;

    static char gacHttpProvDomainName[] = CFG_WLAN_AP_NAME;
    m2m_wifi_start_provision_mode_ext(&apExtConfig, gacHttpProvDomainName, 1);
}

void WiFi_ConStateCb(tenuM2mConnState status)
{
    if (status == M2M_WIFI_CONNECTED)
    {
        if (responseFromProvisionConnect)
        {
            SYS_TIME_TimerStop(softApConnectTaskHandle);
            responseFromProvisionConnect = false;
            ntpTimeFetchTaskHandle       = SYS_TIME_CallbackRegisterMS(ntpTimeFetchTaskcb, 0, CLOUD_NTP_TASK_INTERVAL, SYS_TIME_PERIODIC);
            //iot_connection_completed();
        }

        shared_networking_params.haveAPConnection = 1;
        debug_printGood(" WIFI: Connection Status: CONNECTED");
        CREDENTIALS_STORAGE_clearWifiCredentials();
    }
    else if (status == M2M_WIFI_DISCONNECTED)
    {
        checkBackTaskHandle = SYS_TIME_CallbackRegisterMS(checkBackTaskcb, 0, CLOUD_WIFI_TASK_INTERVAL, SYS_TIME_SINGLE);
        shared_networking_params.amDisconnecting = 1;
    }

    if ((wifiConnectionStateChangedCallback != NULL) && (shared_networking_params.amDisconnecting == 0))
    {
        wifiConnectionStateChangedCallback(status);
    }
}

void WiFi_ProvisionCb(uint8_t sectype, uint8_t* SSID, uint8_t* password)
{
    sprintf((char*)authType, "%d", sectype);
    debug_printInfo("%s", SSID);
    strcpy(ssid, (char*)SSID);
    strcpy(pass, (char*)password);
    debug_printInfo("SOFT AP: Connect Credentials sent to WINC");
    responseFromProvisionConnect = true;
    softApConnectTask();
    softApConnectTaskHandle = SYS_TIME_CallbackRegisterMS(softApConnectTaskcb, 0, SOFT_AP_CONNECT_RETRY_INTERVAL, SYS_TIME_PERIODIC);
}

void wifi_sched(void)
{
    if (ntpTimeFetchTaskTmrExpired == true)
    {
        ntpTimeFetchTaskTmrExpired = false;
        ntpTimeFetchTask();
    }
    if (wifiHandlerTaskTmrExpired == true)
    {
        debug_printWarn(" WIFI: Handler Timeout");
        wifiHandlerTaskTmrExpired = false;
        wifiHandlerTask();
    }
    if (checkBackTaskTmrExpired == true)
    {
        debug_printTrace(" WIFI: Checkback Timeout");
        checkBackTaskTmrExpired = false;
        checkBackTask();
    }
    if (softApConnectTaskTmrExpired == true)
    {
        debug_printWarn(" WIFI: SoftAP Timeout");
        softApConnectTaskTmrExpired = false;
        softApConnectTask();
    }
}

bool wifi_getIpAddressByHostName(uint8_t* host_name)
{
    debug_printGood(" WIFI: Getting IP for %s", host_name);
    return gethostbyname((char*)host_name) == M2M_SUCCESS;
}
