#include <stdio.h>
#include "atca_basic.h"




#define SLOT_BUF_MAX_SIZE  416


extern ATCAIfaceCfg atecc608_1_init_data;
extern ATCAIfaceCfg atecc608_0_init_data;
ATCAIfaceCfg atecc608_init_data;

uint8_t buffdata[SLOT_BUF_MAX_SIZE];

size_t idScope_len;
char* idScope;
uint8_t se_address;


int read_slot8_data(void){

    int ret = 0;
    size_t offst =0;
    ATCA_STATUS status;
    uint8_t lenScope;
    
    
    
    
    printf("Reading Data from Slot 8 ...\n\r");
    status = atcab_init(&atecc608_0_init_data);
    
    if (status != ATCA_SUCCESS)
    {
        printf("atcab_init() failed: %02x\r\n", status);
        return (ret);
    }
    
    
    ret = atcab_read_bytes_zone(ATCA_ZONE_DATA, 8, offst, buffdata, 32);
    
    
     if (ret != ATCA_SUCCESS)
        {
         printf("Read from Slot 8 has Failed %d", ret);
         
         return (ret);
        }
    
    if (ret == ATCA_SUCCESS )
    {
    
    memcpy(&se_address, buffdata, 1);
    
    if (se_address == 0x6A){
        
        memcpy(&lenScope, (buffdata+1), 1); 
        idScope = (char*) malloc(lenScope);
        memcpy(idScope,(buffdata+2), lenScope);
        idScope_len= lenScope;
        atecc608_init_data= atecc608_0_init_data;
        
        }
    else
        {
            
            atcab_init(&atecc608_1_init_data);
            ret = atcab_read_bytes_zone(ATCA_ZONE_DATA, 8, offst, buffdata, 32);
            memcpy(&lenScope, (buffdata+1), 1); 
            idScope = (char*) malloc(lenScope);
            memcpy(idScope,(buffdata+2), lenScope);
            idScope_len= lenScope;
            atecc608_init_data= atecc608_1_init_data;

        }
    

    }
    
    return (ret);
    
}
     





    
