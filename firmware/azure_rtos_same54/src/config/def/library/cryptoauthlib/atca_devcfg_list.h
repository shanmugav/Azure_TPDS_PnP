#include "cryptoauthlib.h"

extern ATCAIfaceCfg atecc608_0_init_data;
extern ATCAIfaceCfg atecc608_1_init_data;

ATCAIfaceCfg *devcfg_list[] = {
    &atecc608_0_init_data,
    &atecc608_1_init_data,
};
