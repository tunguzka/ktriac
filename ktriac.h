/*********************************************
*** Linux kernel module to drive TRIAC with
*** Raspberry Pi
***
*** Written by The TunguZka Team Hungary
*** GNU GPLv3 license
*********************************************/



/***********************************
 * GPIO PIN DEFINITIONS
 * 
 * *********************************/

//GPIO pin of the zerocrossing detection circuit -> low input = zerocrossing
#define GPIO_ACFREQ                     9

//GPIO pin of output circuit, turns TRIAC on
#define GPIO_TRIAC                      10



/***********************************
 * DEFAULT VALUES
 * 
 * *********************************/

//50Hz the default AC_FREQ -> will be set at loading the module
#define AC_DEFAULT_FREQ                  50

/***
 * Zerocross detection tolerance, helpful is the detection circuit noisy is.
 * Not recommendable set to 0
 * Defines the upper and lower time limit of the range in it the input interrupt will be accepted
 * Example: 1% tolerance on 50Hz
 * Irq will accepted and interpreted as zerocrossing event if it comes between 9900-10100us after the last one.
***/
#define AC_DEFAULT_TOLERANCE             3

//If your detection circuit detects with some -/+ latency
#define ZEROCROSS_DEFAULT_LATENCY        800

//100us fire time to be sure that te triac gets on
#define TRIAC_DEFAULT_FIRE_TIME                 100 * 1000


//Enable /dev/ktriac to debug zerocrossing signals
#define DEBUG_DEVICE                    1
