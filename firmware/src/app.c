/*******************************************************************************
 * Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
 *
 * Subject to your compliance with these terms, you may use Microchip software
 * and any derivatives exclusively with Microchip products. It is your
 * responsibility to comply with third party license terms applicable to your
 * use of third party software (including open source software) that may
 * accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
 * ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/

/*******************************************************************************
  MPLAB Harmony Application Source File

  Company:
    Microchip Technology Inc.

  File Name:
    app.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It
    implements the logic of the application's state machine and it may call
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
#include <time.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "click_routines/heartrate9/heartrate9_example.h"
#include "app.h"
#include "wdrv_winc_client_api.h"
#include "iot_config/IoT_Sensor_Node_config.h"
#include "services/iot/cloud/crypto_client/cryptoauthlib_main.h"
#include "services/iot/cloud/crypto_client/crypto_client.h"
#include "services/iot/cloud/cloud_service.h"
#include "services/iot/cloud/wifi_service.h"
#include "services/iot/cloud/bsd_adapter/bsdWINC.h"
#include "credentials_storage/credentials_storage.h"
#include "debug_print.h"
#include "led.h"
#include "azutil.h"
#include "services/iot/cloud/mqtt_packetPopulation/mqtt_packetPopulate.h"
#include "services/iot/cloud/mqtt_packetPopulation/mqtt_iothub_packetPopulate.h"
#include "services/iot/cloud/mqtt_packetPopulation/mqtt_iotprovisioning_packetPopulate.h"
#include "atcacert/atcacert.h"
#include "atcacert/atcacert_def.h"
#include "cryptoauthlib.h"
#include "ecc_types.h"
#include "atcacert/atcacert_client.h"
#include "atcacert/atcacert_pem.h"
#include "drv/driver/m2m_periph.h"
#include "drv/driver/m2m_types.h"
#include "drv/driver/m2m_wifi.h"
#include "../drv/common/nm_common.h"
#include "tng/tng_atcacert_client.h"
#include "tng/tng_atca.h"
#include "tng/tngtls_cert_def_1_signer.h"
#include "tngtls_cert_def_1_signer.h"
#include "tngtls_cert_def_3_device.h"
#include "tng_root_cert.h"
#if CFG_ENABLE_CLI
#include "system/command/sys_command.h"
#endif

#define WLAN_SSID                                "Naruto"
#define WLAN_PSK                                 "2ri4-yowp-ied7"
#if !defined(WLAN_SSID) || !defined(WLAN_PSK)
    #error WLAN/WiFi SSID and Password are not defined! \
            Uncomment defines and replace xxxxxxxxxx with SSID and Password!
#endif


#define MAX_TLS_CERT_LENGTH         1024
#define SIGNER_PUBLIC_KEY_MAX_LEN   64
#define SIGNER_CERT_MAX_LEN         600 //Set Maximum of existing certs
#define DEVICE_CERT_MAX_LEN         600
#define CERT_SN_MAX_LEN             32
#define TLS_SRV_ECDSA_CHAIN_FILE    "ECDSA.lst"
#define INIT_CERT_BUFFER_LEN        (MAX_TLS_CERT_LENGTH*sizeof(uint32_t) - TLS_FILE_NAME_MAX*2 - SIGNER_CERT_MAX_LEN - DEVICE_CERT_MAX_LEN)


// *****************************************************************************
// *****************************************************************************
// Section: Local Function Prototypes
// *****************************************************************************
// *****************************************************************************
static void APP_SendToCloud(void);
static void APP_DataTask(void);
static void APP_WiFiConnectionStateChanged(uint8_t status);
static void APP_ProvisionRespCb(DRV_HANDLE handle, WDRV_WINC_SSID* targetSSID, WDRV_WINC_AUTH_CONTEXT* authCtx, bool status);
static void APP_DHCPAddressEventCb(DRV_HANDLE handle, uint32_t ipAddress);
static void APP_GetTimeNotifyCb(DRV_HANDLE handle, uint32_t timeUTC);
static void APP_ConnectNotifyCb(DRV_HANDLE handle,  WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode);
static const char* bin2hex(const void* data, size_t data_size);
static int8_t winc_certs_append_file_buf(uint32_t* buffer32, uint32_t buffer_size,
                                         const char* file_name, uint32_t file_size,
                                         const uint8_t* file_data);
static void APP_WifiScanTask(DRV_HANDLE handle);
static size_t winc_certs_get_total_files_size(const tstrTlsSrvSecHdr* header);
void transfer_ecc_certs_to_winc(void);
extern LED_STATUS led_status;
static char APP_WiFiApList[APP_BUFFER_SIZE - 1];

#ifdef CFG_MQTT_PROVISIONING_HOST
void iot_provisioning_completed(void);
#endif
// *****************************************************************************
// *****************************************************************************
// Section: Application Macros
// *****************************************************************************
// *****************************************************************************

#define APP_WIFI_SOFT_AP         0
#define APP_WIFI_DEFAULT         1
#define APP_DATATASK_INTERVAL    250L // Each unit is in msec
#define APP_CLOUDTASK_INTERVAL   APP_DATATASK_INTERVAL
#define APP_SW_DEBOUNCE_INTERVAL 1460000L

/* WIFI SSID, AUTH and PWD for AP */
#define APP_CFG_MAIN_WLAN_SSID "Naruto"
#define APP_CFG_MAIN_WLAN_AUTH M2M_WIFI_SEC_WPA_PSK
#define APP_CFG_MAIN_WLAN_PSK  "2ri4-yowp-ied7"

// #define CFG_APP_WINC_DEBUG 1   //define this to print WINC debug messages

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************
#define SN_STRING "sn"

/* Place holder for ECC608A unique serial id */
ATCA_STATUS appCryptoClientSerialNumber;
char*       attDeviceID;
char        attDeviceID_buf[25] = "BAAAAADD1DBAAADD1D";
char        deviceIpAddress[16] = "0.0.0.0";

shared_networking_params_t shared_networking_params;

/* Various NTP Host servers that application relies upon for time sync */
#define WORLDWIDE_NTP_POOL_HOSTNAME "*.pool.ntp.org"
#define ASIA_NTP_POOL_HOSTNAME      "asia.pool.ntp.org"
#define EUROPE_NTP_POOL_HOSTNAME    "europe.pool.ntp.org"
#define NAMERICA_NTP_POOL_HOSTNAME  "north-america.pool.ntp.org"
#define OCEANIA_NTP_POOL_HOSTNAME   "oceania.pool.ntp.org"
#define SAMERICA_NTP_POOL_HOSTNAME  "south-america.pool.ntp.org"
#define NTP_HOSTNAME                "pool.ntp.org"

/* Driver handle for WINC1510 */
static DRV_HANDLE wdrvHandle;
static uint8_t    wifi_mode = WIFI_DEFAULT;

static SYS_TIME_HANDLE App_DataTaskHandle      = SYS_TIME_HANDLE_INVALID;
volatile bool          App_DataTaskTmrExpired  = false;
static SYS_TIME_HANDLE App_CloudTaskHandle     = SYS_TIME_HANDLE_INVALID;
volatile bool          App_CloudTaskTmrExpired = false;
volatile bool          App_WifiScanPending     = false;

static time_t     previousTransmissionTime;
volatile uint32_t telemetryInterval = CFG_DEFAULT_TELEMETRY_INTERVAL_SEC;
extern uint8_t strPatientName[];
volatile bool iothubConnected = false;

extern pf_MQTT_CLIENT    pf_mqtt_iotprovisioning_client;
extern pf_MQTT_CLIENT    pf_mqtt_iothub_client;
extern void              sys_cmd_init();
extern userdata_status_t userdata_status;
uint8_t subject_key_id[20];
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
extern az_iot_pnp_client pnp_client;
#else
extern az_iot_hub_client iothub_client;
#endif

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.

    Application strings and buffers are be defined outside this structure.
*/

APP_DATA appData;

// * PnP Values *
// The model id is the JSON document (also called the Digital Twins Model Identifier or DTMI)
// which defines the capability of your device. The functionality of the device should match what
// is described in the corresponding DTMI. Should you choose to program your own PnP capable device,
// the functionality would need to match the DTMI and you would need to update the below 'model_id'.
// Please see the sample README for more information on this DTMI.
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
const az_span device_model_id_span = AZ_SPAN_LITERAL_FROM_STR(IOT_PLUG_AND_PLAY_MODEL_ID);
#endif

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************
void APP_CloudTaskcb(uintptr_t context)
{
    App_CloudTaskTmrExpired = true;
}

void APP_DataTaskcb(uintptr_t context)
{
    App_DataTaskTmrExpired = true;
}
// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************

// React to the WIFI state change here. Status of 1 means connected, Status of 0 means disconnected
static void APP_WiFiConnectionStateChanged(uint8_t status)
{
     debug_printInfo("  APP: WiFi Connection Status Change to %d", status);
    //  If we have no AP access we want to retry
    if (status != 1)
    {
        // Restart the WIFI module if we get disconnected from the WiFi Access Point (AP)
        CLOUD_reset();
    }
}


// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************



/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */
void APP_Initialize(void)
{
    debug_printInfo("  APP: %s()", __FUNCTION__);
    /* Place the App state machine in its initial state. */
    appData.state     = APP_STATE_CRYPTO_INIT;
    appData.scanState = STATE_SCAN_INIT;

    // uint8_t wifi_mode = WIFI_DEFAULT;
    uint32_t sw0CurrentVal = 0;
    uint32_t sw1CurrentVal = 0;


    previousTransmissionTime = 0;

    debug_init(attDeviceID);

    if (sw0CurrentVal < (APP_SW_DEBOUNCE_INTERVAL / 2))
    {
        if (sw1CurrentVal < (APP_SW_DEBOUNCE_INTERVAL / 2))
        {
            strcpy(ssid, APP_CFG_MAIN_WLAN_SSID);
            strcpy(pass, APP_CFG_MAIN_WLAN_PSK);
            sprintf((char*)authType, "%d", APP_CFG_MAIN_WLAN_AUTH);
        }
        else
        {
            wifi_mode = WIFI_SOFT_AP;
        }
    }
    /* Open I2C driver client */
    sys_cmd_init();   // CLI init

#if (CFG_APP_WINC_DEBUG == 1)
    WDRV_WINC_DebugRegisterCallback(debug_printer);
#endif

    userdata_status.as_uint8 = 0;
    
    
     heartrate9_example_initialize();
    
}

static void APP_ConnectNotifyCb(DRV_HANDLE handle,  WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode)
{
    debug_printTrace("  APP: APP_ConnectNotifyCb %d", currentState);
    WDRV_WINC_SSID ssid;

    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState)
    {
        WiFi_ConStateCb(M2M_WIFI_CONNECTED);
        WDRV_WINC_AssociationSSIDGet(handle, &ssid, NULL);
    }
    else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState)
    {
        switch(errorCode)
        {
            case WDRV_WINC_CONN_ERROR_SCAN:
                debug_printError("  WiFi: DISCONNECTED. Failed to detect AP during scan. Check SSID & authType");
                break;
            case WDRV_WINC_CONN_ERROR_AUTH:
                debug_printError("  WiFi: DISCONNECTED. Failed to authenticate. Check pass phrase");
                break;
            case WDRV_WINC_CONN_ERROR_INPROGRESS:
                break;
            case WDRV_WINC_CONN_ERROR_ASSOC:
            case WDRV_WINC_CONN_ERROR_NOCRED:
            case WDRV_WINC_CONN_ERROR_UNKNOWN:
            default:
                debug_printError("  WiFi: DISCONNECTED, Error Code: %d.  Check WiFi Credentials", errorCode);
        }
        WiFi_ConStateCb(M2M_WIFI_DISCONNECTED);
    }
}

static void APP_GetTimeNotifyCb(DRV_HANDLE handle, uint32_t timeUTC)
{
    // checking > 0 is not recommended, even if getsystime returns null, utctime value will be > 0
    if (timeUTC != 0x86615400U)
    {
        tstrSystemTime pSysTime;
        struct tm      theTime;

        WDRV_WINC_UTCToLocalTime(timeUTC, &pSysTime);
        theTime.tm_hour  = pSysTime.u8Hour;
        theTime.tm_min   = pSysTime.u8Minute;
        theTime.tm_sec   = pSysTime.u8Second;
        theTime.tm_year  = pSysTime.u16Year - 1900;
        theTime.tm_mon   = pSysTime.u8Month - 1;
        theTime.tm_mday  = pSysTime.u8Day;
        theTime.tm_isdst = 0;
        RTC_RTCCTimeSet(&theTime);
    }
}

static void APP_DHCPAddressEventCb(DRV_HANDLE handle, uint32_t ipAddress)
{

    memset(deviceIpAddress, 0, sizeof(deviceIpAddress));

    sprintf(deviceIpAddress, "%lu.%lu.%lu.%lu",
            (0x0FF & (ipAddress)),
            (0x0FF & (ipAddress >> 8)),
            (0x0FF & (ipAddress >> 16)),
            (0x0FF & (ipAddress >> 24)));

    debug_printGood("  APP: DHCP IP Address %s", deviceIpAddress);
    transfer_ecc_certs_to_winc();

    shared_networking_params.haveIpAddress = 1;
    shared_networking_params.haveERROR     = 0;
    shared_networking_params.reported      = 0;
}

static void APP_ProvisionRespCb(DRV_HANDLE              handle,
                                WDRV_WINC_SSID*         targetSSID,
                                WDRV_WINC_AUTH_CONTEXT* authCtx,
                                bool                    status)
{
    uint8_t  sectype;
    uint8_t* ssid;
    uint8_t* password;

    debug_printInfo("  APP: %s()", __FUNCTION__);

    if (status == M2M_SUCCESS)
    {
        sectype  = authCtx->authType;
        ssid     = targetSSID->name;
        password = authCtx->authInfo.WPAPerPSK.key;
        WiFi_ProvisionCb(sectype, ssid, password);
    }
}

void APP_Tasks(void)
{
    switch (appData.state)
    {
        case APP_STATE_CRYPTO_INIT:
        {

            char serialNumber_buf[25];

            shared_networking_params.allBits = 0;
            debug_setPrefix(attDeviceID);
            cryptoauthlib_init();

            if (cryptoDeviceInitialized == false)
            {
                debug_printError("  APP: CryptoAuthInit failed");
            }

#ifdef HUB_DEVICE_ID
            attDeviceID = HUB_DEVICE_ID;
#else
            appCryptoClientSerialNumber = CRYPTO_CLIENT_printSerialNumber(serialNumber_buf);
            if (appCryptoClientSerialNumber != ATCA_SUCCESS)
            {
                switch (appCryptoClientSerialNumber)
                {
                    case ATCA_GEN_FAIL:
                        debug_printError("  APP: DeviceID generation failed, unspecified error");
                        break;
                    case ATCA_BAD_PARAM:
                        debug_printError("  APP: DeviceID generation failed, bad argument");
                    default:
                        debug_printError("  APP: DeviceID generation failed");
                        break;
                }
            }
            else
            {
                // To use Azure provisioning service, attDeviceID should match with the device cert CN,
                // which is the serial number of ECC608 prefixed with "sn" if you are using the
                // the microchip provisioning tool for PIC24.
                strcpy(attDeviceID_buf, SN_STRING);
                strcat(attDeviceID_buf, serialNumber_buf);
                attDeviceID = attDeviceID_buf;
            }
#endif
#if CFG_ENABLE_CLI
            set_deviceId(attDeviceID);
#endif
            debug_setPrefix(attDeviceID);
            CLOUD_setdeviceId(attDeviceID);
            appData.state = APP_STATE_WDRV_INIT;
            break;
        }

        case APP_STATE_WDRV_INIT:
        {
            if (SYS_STATUS_READY == WDRV_WINC_Status(sysObj.drvWifiWinc))
            {
                appData.state = APP_STATE_WDRV_INIT_READY;
            }
            break;
        }

        case APP_STATE_WDRV_INIT_READY:
        {
            wdrvHandle = WDRV_WINC_Open(0, 0);

            if (DRV_HANDLE_INVALID != wdrvHandle)
            {
                appData.state = APP_STATE_WDRV_OPEN;
            }

            break;
        }

        case APP_STATE_WDRV_OPEN:
        {
            WDRV_WINC_CIPHER_SUITE_CONTEXT sCipherSuite;
            m2m_wifi_configure_sntp((uint8_t*)NTP_HOSTNAME, strlen(NTP_HOSTNAME), SNTP_ENABLE_DHCP);
            m2m_wifi_enable_sntp(1);
            WDRV_WINC_DCPT* pDcpt      = (WDRV_WINC_DCPT*)wdrvHandle;
            pDcpt->pCtrl->pfProvConnectInfoCB = APP_ProvisionRespCb;

            debug_printInfo("  APP: WiFi Mode %d", wifi_mode);
            wifi_init(APP_WiFiConnectionStateChanged, wifi_mode);
            transfer_ecc_certs_to_winc();
            sCipherSuite.ciperSuites = SSL_ECC_ALL_CIPHERS;
            WDRV_WINC_SSLActiveCipherSuitesSet(wdrvHandle,&sCipherSuite,NULL);
            debug_printInfo("  APP: transfer completed %d", wifi_mode);

#ifdef CFG_MQTT_PROVISIONING_HOST
            pf_mqtt_iotprovisioning_client.MQTT_CLIENT_task_completed = iot_provisioning_completed;
            CLOUD_init_host(CFG_MQTT_PROVISIONING_HOST, attDeviceID, &pf_mqtt_iotprovisioning_client);
#else
            CLOUD_init_host(hub_hostname, attDeviceID, &pf_mqtt_iothub_client);
#endif   // CFG_MQTT_PROVISIONING_HOST
            if (wifi_mode == WIFI_DEFAULT)
            {
                /* Enable use of DHCP for network configuration, DHCP is the default
                but this also registers the callback for notifications. */
                WDRV_WINC_IPUseDHCPSet(wdrvHandle, &APP_DHCPAddressEventCb);

                debug_printGood("  APP: registering APP_CloudTaskcb");
                App_CloudTaskHandle = SYS_TIME_CallbackRegisterMS(APP_CloudTaskcb, 0, APP_CLOUDTASK_INTERVAL, SYS_TIME_PERIODIC);
                //WDRV_WINC_BSSConnect(wdrvHandle,APP_ConnectNotifyCb);
                  /* Initialize the BSS context to use default values. */
               // WDRV_WINC_BSSCtxSetDefaults(&bssCtx);
               //WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*)WLAN_SSID, strlen(WLAN_SSID));
              // WDRV_WINC_AuthCtxSetWPA(&authCtx, (uint8_t*)WLAN_PSK, strlen(WLAN_PSK));
        
               // WDRV_WINC_BSSDisconnect(wdrvHandle);
                
                //WDRV_WINC_BSSReconnect(wdrvHandle, &APP_ConnectNotifyCb);
                WDRV_WINC_SystemTimeGetCurrent(wdrvHandle, &APP_GetTimeNotifyCb);
                WDRV_WINC_BSSReconnect(wdrvHandle, &APP_ConnectNotifyCb);
                
            }

            appData.state = APP_STATE_WDRV_ACTIV;
            break;
        }

        case APP_STATE_WDRV_ACTIV:
        {
            if (App_CloudTaskTmrExpired == true)
            {
                App_CloudTaskTmrExpired = false;
                CLOUD_task();
            }
            if (App_DataTaskTmrExpired == true)
            {
                App_DataTaskTmrExpired = false;
                APP_DataTask();
             }
            CLOUD_sched();
            wifi_sched();
            MQTT_sched();
            appData.state = APP_STATE_WDRV_ACTIV_2;
            break;
        }
         case APP_STATE_WDRV_ACTIV_1:
         {
            
            CLOUD_sched();
            wifi_sched();
            MQTT_sched();
            break;
         }
          case APP_STATE_WDRV_ACTIV_2:
         {
            if (App_WifiScanPending)
            {
                APP_WifiScanTask(wdrvHandle);
            }
            appData.state = APP_STATE_WDRV_ACTIV;
            break;
         }
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}

// This gets called by the scheduler approximately every 100ms
static void APP_DataTask(void)
{
    // Get the current time. This uses the C standard library time functions
    time_t    timeNow;   // = time(NULL);
    struct tm sys_time;
    RTC_RTCCTimeGet(&sys_time);
    timeNow = mktime(&sys_time);
    // Example of how to send data when MQTT is connected every 1 second based on the system clock
    if (CLOUD_isConnected())
    {
        // How many seconds since the last time this loop ran?
        int32_t delta = difftime(timeNow, previousTransmissionTime);

        

       if (delta >= telemetryInterval)
        {
            previousTransmissionTime = timeNow;
            
            if(led_status == LED_BLINK_STATUS && delta >=1)
            {
                STATUS_LED_Toggle();
            }

            // send telemetry
            APP_SendToCloud();
        }
        
        else if(led_status == LED_BLINK_STATUS && delta >=1)
        {
            previousTransmissionTime = timeNow;
            STATUS_LED_Toggle();
        }


        if (shared_networking_params.reported == 0)
        {
            twin_properties_t twin_properties;

            init_twin_data(&twin_properties);

            strcpy(twin_properties.ip_address, deviceIpAddress);
            twin_properties.flag.ip_address_updated = 1;

            if (az_result_succeeded(send_reported_property(&twin_properties)))
            {
                shared_networking_params.reported = 1;
            }
            else
            {
                debug_printError("  APP: Failed to report IP Address property");
            }
        }


        if (userdata_status.as_uint8 != 0)
        {
            uint8_t i;
            // received user data via UART
            // update reported property
            for (i = 0; i < 8; i++)
            {
                uint8_t tmp = userdata_status.as_uint8;
                if (tmp & (1 << i))
                {
                    debug_printInfo("  APP: User Data Slot %d changed", i + 1);
                }
            }
            userdata_status.as_uint8 = 0;
        }
    }
    else
    {
        debug_printWarn("  APP: Not Connected");
    }


   
}

// *****************************************************************************
// *****************************************************************************
// Section: Functions to interact with Azure IoT Hub and DPS
// *****************************************************************************
// *****************************************************************************

/**********************************************
 * Command (Direct Method)
 **********************************************/
void APP_ReceivedFromCloud_methods(uint8_t* topic, uint8_t* payload)
{
    az_result rc;
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    az_iot_pnp_client_command_request command_request;
#else
    az_iot_hub_client_method_request method_request;
#endif

    debug_printInfo("  APP: %s() Topic %s Payload %s", __FUNCTION__, topic, payload);

    if (topic == NULL)
    {
        debug_printError("  APP: Command topic empty");
        return;
    }

    az_span command_topic_span = az_span_create(topic, strlen((char*)topic));

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
    rc = az_iot_pnp_client_commands_parse_received_topic(&pnp_client, command_topic_span, &command_request);
#else
    rc = az_iot_hub_client_methods_parse_received_topic(&iothub_client, command_topic_span, &method_request);
#endif

    if (az_result_succeeded(rc))
    {
        debug_printTrace("  APP: Command Topic  : %s", az_span_ptr(command_topic_span));
#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
        debug_printTrace("  APP: Command Name   : %s", az_span_ptr(command_request.command_name));
#else
        debug_printTrace("  APP: Method Name   : %s", az_span_ptr(method_request.name));
#endif
        debug_printTrace("  APP: Command Payload: %s", (char*)payload);

#ifdef IOT_PLUG_AND_PLAY_MODEL_ID
        process_direct_method_command(payload, &command_request);
#else
        process_direct_method_command(payload, &method_request);
#endif
    }
    else
    {
        debug_printError("  APP: Command from unknown topic: '%s' return code 0x%08x.", az_span_ptr(command_topic_span), rc);
    }
}

/**********************************************
 * Properties (Device Twin)
 **********************************************/
void APP_ReceivedFromCloud_patch(uint8_t* topic, uint8_t* payload)
{
    az_result         rc;
    twin_properties_t twin_properties;

    init_twin_data(&twin_properties);

    twin_properties.flag.is_initial_get = 0;

    debug_printInfo("  APP: %s() Payload %s", __FUNCTION__, payload);

    if (az_result_failed(rc = process_device_twin_property(topic, payload, &twin_properties)))
    {
        // If the item can't be found, the desired temp might not be set so take no action
        debug_printError("  APP: Could not parse desired property, return code 0x%08x\n", rc);
    }
    else
    {
        if (twin_properties.flag.led_found == 1)
        {
            debug_printInfo("  APP: Twin 1 Found led_status Value");
        }

        if (twin_properties.flag.telemetry_interval_found == 1)
        {
            debug_printInfo("  APP: Found telemetryInterval value '%d'", telemetryInterval);
        }
        
         if (twin_properties.flag.patient_name_found == 1)
        {
            debug_printInfo("  APP: Found Patient Name value %s", twin_properties.desired_patientName);
        }
        
          if (twin_properties.flag.debug_level_found == 1)
        {
            debug_printInfo("  APP: Found debug level  value '%d'", twin_properties.desired_debugLevel);
        }
        
        rc = send_reported_property_twin(&twin_properties);

        if (az_result_failed(rc))
        {
            debug_printError("  APP: send_reported_property() failed 0x%08x", rc);
        }
    }
}


void APP_ReceivedFromCloud_twin(uint8_t* topic, uint8_t* payload)
{
    az_result         rc=AZ_OK;
    twin_properties_t twin_properties;

    init_twin_data(&twin_properties);

    if (topic == NULL)
    {
        debug_printWarn("  APP: Twin topic empty");
        return;
    }

    debug_printTrace("  APP: %s() Payload %s", __FUNCTION__, payload);

    if (az_result_failed(rc = process_device_twin_property(topic, payload, &twin_properties)))
    {
        // If the item can't be found, the desired temp might not be set so take no action
        debug_printError("  APP Twin: Could not parse desired property, return code 0x%08x\n", rc);
    }
    else
    {
        if (twin_properties.flag.is_initial_get)
        {
            iothubConnected = true;
        }

        if (twin_properties.flag.led_found == 1)
        {
            debug_printInfo("  APP: Found led_status Value ");
        }

        if (twin_properties.flag.telemetry_interval_found == 1)
        {
            debug_printInfo("  APP: Found telemetryInterval Value '%d'", telemetryInterval);
        }
        
         if (twin_properties.flag.debug_level_found == 1)
        {
            debug_printInfo("  APP: Found debug Level Value '%d'", twin_properties.desired_debugLevel);
        }
        
        if (twin_properties.flag.patient_name_found == 1)
        {
            debug_printInfo("  APP: Found Patient Name Found Value '%s'", twin_properties.desired_patientName);
        }
        
        rc = send_reported_property(&twin_properties);

        if (az_result_failed(rc))
        {
            debug_printError("  APP: send_reported_property() failed 0x%08x", rc);
        }
    }

    // debug_printInfo("  APP: << %s() rc = 0x%08x", __FUNCTION__, rc);
}

/**********************************************
 * Read Temperature Sensor value
 **********************************************/



/**********************************************
 * Entry point for telemetry
 **********************************************/
void APP_SendToCloud(void)
{
    if (iothubConnected)
    {
        send_telemetry_message();
    }
}

/**********************************************
 * Callback functions for MQTT
 **********************************************/
void iot_connection_completed(void)
{
    debug_printGood("  APP: %s()", __FUNCTION__); 
    App_DataTaskHandle = SYS_TIME_CallbackRegisterMS(APP_DataTaskcb, 0, APP_DATATASK_INTERVAL, SYS_TIME_PERIODIC);
}

#ifdef CFG_MQTT_PROVISIONING_HOST
void iot_provisioning_completed(void)
{
    debug_printGood("  APP: %s()", __FUNCTION__);
    pf_mqtt_iothub_client.MQTT_CLIENT_task_completed = iot_connection_completed;
    CLOUD_init_host(hub_hostname, attDeviceID, &pf_mqtt_iothub_client);
    CLOUD_reset();
}
#endif   // CFG_MQTT_PROVISIONING_HOST

void APP_WifiScan(char* buffer)
{
    App_WifiScanPending = true;
    sprintf(buffer, "OK");
}

void APP_WifiGetStatus(char* buffer)
{
    WDRV_WINC_SSID   ssid;
    WDRV_WINC_STATUS status;

    status = WDRV_WINC_AssociationSSIDGet(wdrvHandle, &ssid, NULL);

    if (status == WDRV_WINC_STATUS_OK)
    {
        sprintf(buffer, "%s,%s", deviceIpAddress, ssid.name);
    }
    else
    {
        // debug_printError("  APP: WDRV_WINC_AssociationSSIDGet failed %d", status);
        sprintf(buffer, "0.0.0.0");
    }
}

static void APP_WifiScanTask(DRV_HANDLE handle)
{
    WDRV_WINC_STATUS status;
    switch (appData.scanState)
    {
        case STATE_SCAN_INIT:

            memset(APP_WiFiApList, 0, sizeof(APP_WiFiApList));

            
           if (WDRV_WINC_STATUS_OK == WDRV_WINC_BSSFindFirst(wdrvHandle, WDRV_WINC_ALL_CHANNELS, true, NULL, NULL))
            {
                appData.scanState = STATE_SCANNING;
            }
            else
            {
                appData.scanState = STATE_SCAN_ERROR;
            }
            break;

        case STATE_SCANNING:
            if (false == WDRV_WINC_BSSFindInProgress(handle))
            {
                appData.scanState = STATE_SCAN_GET_RESULTS;
            }
            break;

        case STATE_SCAN_GET_RESULTS:
        {
            WDRV_WINC_BSS_INFO BSSInfo;
            if (WDRV_WINC_STATUS_OK == WDRV_WINC_BSSFindGetInfo(handle, &BSSInfo))
            {
                status = WDRV_WINC_BSSFindNext(handle, NULL);
                if (WDRV_WINC_STATUS_BSS_FIND_END == status)
                {
                    appData.scanState = STATE_SCAN_DONE;
                    break;
                }
                else if (status != WDRV_WINC_STATUS_OK)
                {
                    appData.scanState = STATE_SCAN_ERROR;
                    break;
                }
                else if (BSSInfo.ctx.ssid.length > 0)
                {
                    if (strlen(APP_WiFiApList) > 0)
                    {
                        strlcat(APP_WiFiApList, ",", sizeof(APP_WiFiApList) - 1);
                    }
                    strlcat(APP_WiFiApList, (const char*)&BSSInfo.ctx.ssid.name, sizeof(APP_WiFiApList) - 1);
                }
            }
            break;
        }

        case STATE_SCAN_DONE:
            App_WifiScanPending = false;
            strlcat(APP_WiFiApList, "\4", sizeof(APP_WiFiApList));
            debug_disable(true);
            SYS_CONSOLE_Message(0, APP_WiFiApList);
            debug_disable(false);
            break;

        case STATE_SCAN_ERROR:
            App_WifiScanPending = false;
            debug_printError("  APP: Scan failed");
            break;
    }
}

static void WriteCert(int type, const char *pem_cert,size_t pem_cert_size)
{
    uint8_t root_cert[1024];
    size_t root_cert_size;
    int atca_status = ATCACERT_E_SUCCESS;
    //use enum instead of int
    if(type == 0)
    {
       debug_printGood("TNG Device Cert : \r\n\r\n");
        SYS_CONSOLE_Write(0, pem_cert, pem_cert_size); 
    }
    else if (type == 1)
    {
        debug_printGood("TNG Signer Cert : \r\n\r\n");
        SYS_CONSOLE_Write(0, pem_cert, pem_cert_size);  
    }
    else
    {
        debug_printGood("TNG Root Cert : \r\n\r\n");
        #define PEM_HEADER "-----BEGIN CERTIFICATE-----\n\r"
        #define PEM_FOOTER "\n\r-----END CERTIFICATE-----\n\r"

         if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_max_device_cert_size(&root_cert_size)))
        {
            return ;
        }
     
   
        if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_root_cert(root_cert, &root_cert_size)))
        {
            return ;
        }
        //atcacert_encode_pem_cert(root_cert, root_cert_size, pem_cert, &pem_cert_size);
        atcab_base64encode(root_cert, root_cert_size, pem_cert, &pem_cert_size);

        SYS_CONSOLE_Write(0, PEM_HEADER, strlen(PEM_HEADER));
        SYS_CONSOLE_Write(0, pem_cert, pem_cert_size);
        SYS_CONSOLE_Write(0, PEM_FOOTER, strlen(PEM_FOOTER)-1);
         
    }
    
}

int8_t ecc_transfer_certificates()
{


    //#define CLOUD_CONNECT_WITH_CUSTOM_CERTS    
    int8_t status = M2M_SUCCESS;
    int atca_status = ATCACERT_E_SUCCESS;
    uint8_t *signer_cert = NULL;
    size_t signer_cert_size;
    static bool btransferDone=false;

#ifndef CLOUD_CONNECT_WITH_CUSTOM_CERTS
    uint8_t public_key[SIGNER_PUBLIC_KEY_MAX_LEN];
#endif
    uint8_t *device_cert = NULL;
    size_t device_cert_size;
    uint8_t cert_sn[CERT_SN_MAX_LEN];
    size_t cert_sn_size;
    uint8_t *file_list = NULL;
    char *device_cert_filename = NULL;
    char *signer_cert_filename = NULL;
    uint32_t sector_buffer[MAX_TLS_CERT_LENGTH];
    char pem_cert[1024];
    size_t pem_cert_size;
    const atcacert_def_t *device_cert_def, *signer_cert_def;

    do
    {
        if(btransferDone == true)
           return status;
     
        // Clear cert chain buffer
        memset(sector_buffer, 0xFF, sizeof(sector_buffer));

        // Use the end of the sector buffer to temporarily hold the data to save RAM
        file_list   = ((uint8_t*)sector_buffer) + (sizeof(sector_buffer) - TLS_FILE_NAME_MAX * 2);
        signer_cert = file_list - SIGNER_CERT_MAX_LEN;
        device_cert = signer_cert - DEVICE_CERT_MAX_LEN;

        // Init the file list
        memset(file_list, 0, TLS_FILE_NAME_MAX * 2);
        device_cert_filename = (char*)&file_list[0];
        signer_cert_filename = (char*)&file_list[TLS_FILE_NAME_MAX];

    #ifndef CLOUD_CONNECT_WITH_CUSTOM_CERTS
        // Uncompress the signer certificate from the ATECCx08A device
        
        tng_atcacert_root_public_key(public_key);
        signer_cert_size = SIGNER_CERT_MAX_LEN;
        if(ATCACERT_E_SUCCESS != (atca_status = atcacert_read_cert(&g_tngtls_cert_def_1_signer, public_key,
                                        signer_cert, &signer_cert_size)))
        {
            break;
        }
        pem_cert_size = sizeof(pem_cert);
        atcacert_encode_pem_cert(signer_cert, signer_cert_size, pem_cert, &pem_cert_size);
        WriteCert(1,pem_cert,pem_cert_size);
        //debug_printGood("Signer Cert : \r\n%s\r\n", pem_cert);

        // Get the signer's public key from its certificate
        tng_atcacert_signer_public_key(public_key,NULL);

        // Uncompress the device certificate from the ATECCx08A device.
        device_cert_size = DEVICE_CERT_MAX_LEN;
        if(ATCACERT_E_SUCCESS != (atca_status = atcacert_read_cert(&g_tngtls_cert_def_3_device,public_key,
                                        device_cert, &device_cert_size)))
        {
            break;
        }
        pem_cert_size = sizeof(pem_cert);
        atcacert_encode_pem_cert(device_cert, device_cert_size, pem_cert, &pem_cert_size);
        //debug_printGood("Device Cert : \r\n%s\r\n", pem_cert);
        WriteCert(0,pem_cert,pem_cert_size);

        if(ATCACERT_E_SUCCESS != (atca_status = atcacert_get_subj_key_id(&g_tngtls_cert_def_3_device, device_cert,
                                        device_cert_size, subject_key_id)))
        {
            // Break the do/while loop
            break;
        }

        signer_cert_def = &g_tngtls_cert_def_1_signer;
        device_cert_def = &g_tngtls_cert_def_3_device;
    #else
        // Uncompress the signer certificate from the ATECCx08A device
        if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_max_signer_cert_size(&signer_cert_size)))
        {
            break;
        }

        if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_read_signer_cert(signer_cert, &signer_cert_size)))
        {
            break;
        }
        pem_cert_size = sizeof(pem_cert);
        atcacert_encode_pem_cert(signer_cert, signer_cert_size, pem_cert, &pem_cert_size);
        debug_printGood("Signer Cert : \r\n%s\r\n", pem_cert);

        // Uncompress the device certificate from the ATECCx08A device.
        if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_max_device_cert_size(&device_cert_size)))
        {
            break;
        }
        if(ATCACERT_E_SUCCESS != (atca_status = tng_atcacert_read_device_cert(device_cert, &device_cert_size, NULL)))
        {
            break;
        }
        pem_cert_size = sizeof(pem_cert);
        atcacert_encode_pem_cert(device_cert, device_cert_size, pem_cert, &pem_cert_size);
        debug_printGood("Device Cert : \r\n%s\r\n", pem_cert);

        
        if(ATCA_SUCCESS != (atca_status = tng_get_device_cert_def(&device_cert_def)))
        {
            break;
        }
    #endif

        // Get the device certificate SN for the filename
        cert_sn_size = sizeof(cert_sn);
        if(ATCACERT_E_SUCCESS != (atca_status = atcacert_get_cert_sn(device_cert_def, device_cert,
                                           device_cert_size, cert_sn, &cert_sn_size)))
        {
            break;
        }

        // Build the device certificate filename
        strcpy(device_cert_filename, "CERT_");
        strcat(device_cert_filename, bin2hex(cert_sn, cert_sn_size));

        // Add the DER device certificate the TLS certs buffer
        status = winc_certs_append_file_buf(sector_buffer, sizeof(sector_buffer),
                                            device_cert_filename, device_cert_size, device_cert);
        if (status != M2M_SUCCESS)
        {
            // Break the do/while loop
            break;
        }

        device_cert = NULL; // Make sure we don't use this now that it has moved

        // Get the signer certificate SN for the filename
        cert_sn_size = sizeof(cert_sn);
        if(ATCACERT_E_SUCCESS != (atca_status = atcacert_get_cert_sn(signer_cert_def, signer_cert,
                                           signer_cert_size, cert_sn, &cert_sn_size)))
        {
            // Break the do/while loop
            break;
        }


        // Build the signer certificate filename
        strcpy(signer_cert_filename, "CERT_");
        strcat(signer_cert_filename, bin2hex(cert_sn, cert_sn_size));

        // Add the DER signer certificate the TLS certs buffer
        status = winc_certs_append_file_buf(sector_buffer, sizeof(sector_buffer),
                                            signer_cert_filename, signer_cert_size, signer_cert);
        if (status != M2M_SUCCESS)
        {
            // Break the do/while loop
            break;
        }

        // Add the cert chain list file to the TLS certs buffer
        status = winc_certs_append_file_buf(sector_buffer, sizeof(sector_buffer),
                                            TLS_SRV_ECDSA_CHAIN_FILE, TLS_FILE_NAME_MAX * 2, file_list);
        if (status != M2M_SUCCESS)
        {
            // Break the do/while loop
            break;
        }

        file_list = NULL;
        signer_cert_filename = NULL;
        device_cert_filename = NULL;

        // Update the TLS cert chain on the WINC.
        status = m2m_ssl_send_certs_to_winc((uint8_t*)sector_buffer,
                                            (uint32_t)winc_certs_get_total_files_size((tstrTlsSrvSecHdr*)sector_buffer));
        if (status != M2M_SUCCESS)
        {
            // Break the do/while loop
            break;
        }
    }
    while (false);

    if (atca_status)
    {
        M2M_ERR("eccSendCertsToWINC() failed with ret=%d", atca_status);
        status =  M2M_ERR_FAIL;
    }
    
    btransferDone = true;

    return status;
}

void transfer_ecc_certs_to_winc(void)
{
   

    do
    {
        if (ecc_transfer_certificates() != M2M_SUCCESS)
        {
            break;
        }

       
    }
    while (0);

    return ;
}

static int8_t winc_certs_append_file_buf(uint32_t* buffer32, uint32_t buffer_size,
                                         const char* file_name, uint32_t file_size,
                                         const uint8_t* file_data)
{
    tstrTlsSrvSecHdr* header = (tstrTlsSrvSecHdr*)buffer32;
    tstrTlsSrvSecFileEntry* file_entry = NULL;
    uint16_t str_size = (uint8_t)strlen((char*)file_name) + 1;
    uint16_t count = 0;
    uint8_t *pBuffer = (uint8_t*)buffer32;

    while ((*pBuffer) == 0xFF)
    {

        if (count == INIT_CERT_BUFFER_LEN)
        {
            break;
        }
        count++;
        pBuffer++;
    }

    if (count == INIT_CERT_BUFFER_LEN)
    {
        // The WINC will need to add the reference start pattern to the header
        header->u32nEntries = 0;                    // No certs
        // The WINC will need to add the offset of the flash were the certificates are stored to this address
        header->u32NextWriteAddr = sizeof(*header); // Next cert will be written after the header
    }

    if (header->u32nEntries >= sizeof(header->astrEntries) / sizeof(header->astrEntries[0]))
    {
        return M2M_ERR_FAIL; // Already at max number of files

    }
    if ((header->u32NextWriteAddr + file_size) > buffer_size)
    {
        return M2M_ERR_FAIL; // Not enough space in buffer for new file

    }
    file_entry = &header->astrEntries[header->u32nEntries];
    header->u32nEntries++;

    if (str_size > sizeof(file_entry->acFileName))
    {
        return M2M_ERR_FAIL; // File name too long
    }
    memcpy((uint8_t*)file_entry->acFileName, (uint8_t*)file_name, str_size);

    file_entry->u32FileSize = file_size;
    file_entry->u32FileAddr = header->u32NextWriteAddr;
    header->u32NextWriteAddr += file_size;

    // Use memmove to accommodate optimizations where the file data is temporarily stored
    // in buffer32
    memmove(((uint8_t*)buffer32) + (file_entry->u32FileAddr), (uint8_t*)file_data, file_size);

    return M2M_SUCCESS;
}

static const char* bin2hex(const void* data, size_t data_size)
{
    static char buf[256];
    static char hex[] = "0123456789abcdef";
    const uint8_t* data8 = data;

    if (data_size * 2 > sizeof(buf) - 1)
    {
        return "[buf too small]";
    }

    for (size_t i = 0; i < data_size; i++)
    {
        buf[i * 2 + 0] = hex[(*data8) >> 4];
        buf[i * 2 + 1] = hex[(*data8) & 0xF];
        data8++;
    }
    buf[data_size * 2] = 0;

    return buf;
}
static size_t winc_certs_get_total_files_size(const tstrTlsSrvSecHdr* header)
{
    uint8_t *pBuffer = (uint8_t*)header;
    uint16_t count = 0;

    while ((*pBuffer) == 0xFF)
    {

        if (count == INIT_CERT_BUFFER_LEN)
        {
            break;
        }
        count++;
        pBuffer++;
    }

    if (count == INIT_CERT_BUFFER_LEN)
    {
        return sizeof(*header); // Buffer is empty, no files

    }
    return header->u32NextWriteAddr;
}
/*******************************************************************************
 End of File
 */
