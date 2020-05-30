/*********************************************
*** ramp utility to deal with ktriac kernel module
*** 
*** Written by The TunguZka Team Hungary
*** GNU GPLv3 license
*********************************************/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>


struct ramp_t {
    unsigned int from;
    float inc;
    unsigned int wait;
    unsigned int steps;
};

#define MAX_RAMP    10
struct ramp_t ramp[MAX_RAMP];
unsigned int ramp_count = 0;

const char* usage = "expected format: [from]%-[to]%-steps-duration [ARGS]\n\nArguments:\n-n: the actual percentage\n";


/*inline*/ void out_of_range( const char* err, int from, int to, int value)
{
    printf("%s is out of range: %d [%d-%d]\n", err, value, from, to);
}

/*inline*/ const char* get_parameter_argument( const char * parameter, char **argv, int index, int count)
{
    if ( index < 0 || index + 1 >= count)
        return NULL;
    
    if ( strcmp( parameter, argv[index]) == 0)
        return argv[ index + 1];
    
    return NULL;
}

int main( int argc, char **argv)
{
    
    //good start for my ansycron pump:
    //./ramp 0%-70%-10%-77 70%-90%-1%-1000 90%-100%-1%-1000
    
    int act_percent = -1;
    
    if ( argc <= 1)
    {
        printf("%s\n", usage);
        exit( EXIT_FAILURE);
    }
    
    //build ramp list
    for ( int i = 1; i < argc; ++i)
    {
        int from, to, steps, time;
        const char* parameter = NULL;
        
        if ( sscanf( argv[i], "%d%%-%d%%-%d%%-%d", &from, &to, &steps, &time) < 4)
        {
            if ( ( parameter = get_parameter_argument( "-n", argv, i, argc)) != NULL)
            {
                ++i;
                int ret = sscanf( parameter, "%d", &act_percent);
                if ( !ret || act_percent < 0 || act_percent > 100)
                {
                    printf("Wrong argument: -n expects [NUM: 0-100] to specify the actual percent\n");
                    exit( EXIT_FAILURE);
                }
                continue;
            }
            
            printf("Wrong argument: %s\n%s\n", argv[i], usage);
            exit( EXIT_FAILURE);
        }

        if ( from < 0 || from > 100)
        {
            out_of_range( "argument [from value]", 0, 100, from);
            exit( EXIT_FAILURE);
        }
        
        if ( to < 0 || to > 100)
        {
            out_of_range( "argument [to value]", 0, 100, to);
            exit( EXIT_FAILURE);
        }
            
        if ( steps < 0)
        {
            out_of_range( "argument [steps value]", 0, 1000000, steps);
            exit( EXIT_FAILURE);
        }
            
        if ( time < 0)
        {
            out_of_range( "argument [time value]", 0, 1000000, time);
            exit( EXIT_FAILURE);
        }
            
        ramp[ ramp_count].from = from;

        if ( from == to)
            ramp[ ramp_count].steps = 1;
        else
            ramp[ ramp_count].steps = fabsf( to - from) / (float)steps;

        ramp[ ramp_count].inc = ( float)( to - from) / ( float) ramp[ ramp_count].steps;
        ramp[ ramp_count].wait = time * 1000 / ramp[ ramp_count].steps;
        
//        printf("from: %d\ninc: %5.2f\nsteps: %d\nwait:%d\n", ramp[ ramp_count].from, ramp[ ramp_count].inc, ramp[ ramp_count].steps, ramp[ ramp_count].wait);
        
        ++ramp_count;
        if ( ramp_count >= MAX_RAMP)
        {
            printf("Error: too many ramps...max %d ramps\n", MAX_RAMP);
            exit( EXIT_FAILURE);
        }
    }

    if ( act_percent >= 0 && ramp_count)
    {
        float value = ramp[0].from;
        register int inc = ( ramp[0].inc > 0);
        
        for ( int j = 0; j < ramp[0].steps; ++j)
        {
            value += ramp[0].inc;
            if ( ( inc && value >= act_percent) || ( !inc && value <= act_percent))
            {
                ramp[0].steps -= j;
                ramp[0].from = value - ramp[0].inc;
                break;
            }
        }
        
        if ( ( inc && value < act_percent) || ( !inc && value > act_percent))
        {
            ramp[0].from = value - ramp[0].inc;
            ramp[0].steps = 1;
        }
    }
    
    for ( int i = 0; i < ramp_count; ++i)
    {
        float value = ramp[i].from;
        for ( int j = 0; j < ramp[i].steps; ++j)
        {            
            value += ramp[i].inc;
            printf("%d%%\n", (int) value);
            fflush( stdout);
            usleep( ramp[i].wait);
        }
    }
    
    return EXIT_SUCCESS;
}

