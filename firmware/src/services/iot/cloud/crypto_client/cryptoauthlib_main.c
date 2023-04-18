#include "cryptoauthlib_main.h"
#include "crypto_client.h"
#include "cryptoauthlib.h"
#include "definitions.h"

extern ATCAIfaceCfg atecc608_0_init_data;
void cryptoauthlib_init(void)
{
    uint8_t rv;
    atecc608_0_init_data.atcai2c.address = SECURE_ELEMENT_ADDRESS;
    rv = atcab_init(&atecc608_0_init_data);
    

    if (rv != ATCA_SUCCESS)
    {
        cryptoDeviceInitialized = false;
    }
    else
    {
        atcab_lock_data_slot(0);
        cryptoDeviceInitialized = true;
    }
}
