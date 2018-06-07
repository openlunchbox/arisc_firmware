/**
 * @file    mod_pulsgen.c
 *
 * @brief   pulses generator module
 *
 * This module implements an API
 * to make real-time pulses generation using GPIO
 */

#include "mod_timer.h"
#include "mod_gpio.h"
#include "mod_pulsgen.h"




// private vars

static uint8_t max_id = 0; // maximum channel id
static struct pulsgen_ch_t gen[PULSGEN_CH_CNT] = {0}; // array of channels data
static uint8_t msg_buf[PULSGEN_MSG_BUF_LEN] = {0};




// public methods

/**
 * @brief   module init
 * @note    call this function only once before pulsgen_module_base_thread()
 * @retval  none
 */
void pulsgen_module_init()
{
    TIMER_START();
}

/**
 * @brief   module base thread
 * @note    call this function in the main loop, before gpio_module_base_thread()
 * @retval  none
 */
void pulsgen_module_base_thread()
{
    static uint8_t c;
    static uint32_t tick = 0, tick_prev = 0, tick_ovrfl = 0, todo_tick = 0;

    // get current CPU tick
    tick = TIMER_CNT_GET();

    // tick value overflow check
    tick_ovrfl = tick < tick_prev ? 1 : 0;

    // check all working channels
    for ( c = max_id + 1; c--; )
    {
        if ( !gen[c].task ) continue; // if channel disabled, goto next channel

        if ( !gen[c].task_infinite && !gen[c].task_toggles_todo ) // if we have no steps to do
        {
            gen[c].task = 0; // disable channel
            if ( max_id && c == max_id ) --max_id; // if needed decrease channels max ID value
            continue; // goto next channel
        }

        // pulse change time check
        if ( gen[c].todo_tick_ovrfl )
        {
            if ( tick_ovrfl ) gen[c].todo_tick_ovrfl = 0;
            continue;
        }
        else if ( tick < gen[c].todo_tick ) continue;

        todo_tick = gen[c].todo_tick; // save current to do tick value

        if ( gen[c].pin_state ) // if current pin state is HIGH
        {
            gen[c].pin_state = 0; // set pin state to LOW
            gen[c].todo_tick += gen[c].setup_ticks; // set new timestamp
        }
        else // if current pin state is LOW
        {
            gen[c].pin_state = 1; // set step state to HIGH
            gen[c].todo_tick += gen[c].hold_ticks; // set new timestamp
        }

        // set timestamp overflow flag
        gen[c].todo_tick_ovrfl = gen[c].todo_tick < todo_tick ? 1 : 0;

        --gen[c].task_toggles_todo; // decrease number of pin changes to do

        // toggle pin
        if ( gen[c].pin_state ^ gen[c].pin_inverted )
        {
            gpio_pin_clear(gen[c].port, gen[c].pin);
        }
        else
        {
            gpio_pin_set(gen[c].port, gen[c].pin);
        }
    }

    // save current tick value
    tick_prev = tick;
}




/**
 * @brief   setup GPIO pin for the selected channel
 *
 * @param   c           channel id
 * @param   port        GPIO port number
 * @param   pin         GPIO pin number
 * @param   inverted    invert pin state?
 *
 * @retval  none
 */
void pulsgen_pin_setup(uint8_t c, uint8_t port, uint8_t pin, uint8_t inverted)
{
    gpio_pin_setup_for_output(port, pin);

    gen[c].port = port;
    gen[c].pin = pin;
    gen[c].pin_inverted = inverted;
    gen[c].pin_state = 0;

    // set pin state
    if ( gen[c].pin_state ^ gen[c].pin_inverted )
    {
        gpio_pin_clear(gen[c].port, gen[c].pin);
    }
    else
    {
        gpio_pin_set(gen[c].port, gen[c].pin);
    }
}




/**
 * @brief   setup a new task for the selected channel
 *
 * @param   c           channel id
 * @param   period      pin state change period (in microseconds)
 * @param   toggles     number of pin state changes
 * @param   duty        duty cycle value (0..PULSGEN_MAX_DUTY)
 * @param   delay       task start delay (in microseconds)
 *
 * @retval  none
 */
void pulsgen_task_setup(uint8_t c, uint32_t period, uint32_t toggles, uint8_t duty, uint32_t delay)
{
    static uint32_t tick = 0, period_ticks = 0;

    // important checks
    if ( !period || !duty ) return;

    // get current CPU tick
    tick = TIMER_CNT_GET();

    if ( c > max_id ) ++max_id;

    // set task data
    gen[c].task = 1;
    gen[c].task_infinite = toggles ? 1 : 0;
    gen[c].task_toggles = toggles ? toggles : UINT32_MAX;
    gen[c].task_toggles_todo = gen[c].task_toggles;

    // soft checks
    if ( period >= PULSGEN_MAX_PERIOD ) period  = PULSGEN_MAX_PERIOD - 1;
    if ( duty   >= PULSGEN_MAX_DUTY )   duty    = PULSGEN_MAX_DUTY   - 1;

    period_ticks = (TIMER_FREQUENCY / 1000000) * period;

    // uin32_t overflow fix
    gen[c].hold_ticks = period_ticks < (UINT32_MAX/PULSGEN_MAX_DUTY) ?
        period_ticks * duty / PULSGEN_MAX_DUTY :
        period_ticks / PULSGEN_MAX_DUTY * duty ;

    gen[c].setup_ticks = period_ticks - gen[c].hold_ticks;
    gen[c].todo_tick = tick + gen[c].setup_ticks;

    // if we need a delay before task start
    if ( delay )
    {
        if ( delay >= PULSGEN_MAX_PERIOD ) delay = PULSGEN_MAX_PERIOD - 1;
        gen[c].todo_tick += (TIMER_FREQUENCY / 1000000) * delay;
    }

    gen[c].todo_tick_ovrfl = gen[c].todo_tick < tick ? 1 : 0;
}

/**
 * @brief   abort current task for the selected channel
 * @param   c       channel id
 * @retval  none
 */
void pulsgen_task_abort(uint8_t c)
{
    gen[c].task = 0;

    if ( max_id && c == max_id ) --max_id;
}




/**
 * @brief   get current task state for the selected channel
 *
 * @param   c   channel id
 *
 * @retval  0   (channel have no task)
 * @retval  1   (channel have a task)
 */
uint8_t pulsgen_task_state(uint8_t c)
{
    return gen[c].task;
}

/**
 * @brief   get current pin state changes since task start
 * @param   c   channel id
 * @retval  0..0xFFFFFFFF
 */
uint32_t pulsgen_task_toggles(uint8_t c)
{
    return gen[c].task_toggles - gen[c].task_toggles_todo;
}




/**
 * @brief   "message received" callback
 *
 * @note    this function will be called automatically
 *          when a new message will arrive for this module.
 *
 * @param   type    user defined message type (0..0xFF)
 * @param   msg     pointer to the message buffer
 * @param   length  the length of a message (0 .. MSG_LEN)
 *
 * @retval   0 (message read)
 * @retval  -1 (message not read)
 */
int8_t volatile pulsgen_msg_recv(uint8_t type, uint8_t * msg, uint8_t length)
{
    static uint8_t i = 0;

    switch (type)
    {
        case PULSGEN_MSG_PIN_SETUP:
        {
            // setup input message view
            struct pulsgen_msg_pin_setup_t* data = (struct pulsgen_msg_pin_setup_t*) msg;

            // setup channel pins
            for ( i = PULSGEN_CH_CNT; i--; )
            {
                if ( data->channels_mask & (1U << i) )
                {
                    pulsgen_pin_setup( i, data->port[i], data->pin[i],
                        (data->inverted_mask & (1U << i)) ? 1 : 0 );
                }
            }

            break;
        }

        case PULSGEN_MSG_TASK_SETUP:
        {
            // setup input message view
            struct pulsgen_msg_task_setup_t* data = (struct pulsgen_msg_task_setup_t*) msg;

            // select channels mask
            uint32_t mask = data->channels_mask1 ? data->channels_mask1 : data->channels_mask2;

            // setup channel tasks
            for ( i = PULSGEN_MSG_TASK_SETUP_CH_CNT; i--; )
            {
                if ( mask & (1U << i) )
                {
                    pulsgen_task_setup
                    (
                        data->channels_mask1 ? i : i + PULSGEN_MSG_TASK_SETUP_CH_CNT,
                        data->period[i], data->toggles[i],
                        data->duty[i],   data->delay[i]
                    );
                }
            }

            break;
        }

        case PULSGEN_MSG_TASK_ABORT:
        {
            // setup input message view
            struct pulsgen_msg_task_abort_t* data = (struct pulsgen_msg_task_abort_t*) msg;

            // abort tasks
            for ( i = PULSGEN_CH_CNT; i--; )
            {
                if ( data->channels_mask & (1U << i) ) pulsgen_task_abort(i);
            }

            break;
        }

        case PULSGEN_MSG_TASK_STATE:
        {
            // setup messages view
            struct pulsgen_msg_task_state_t*  in = (struct pulsgen_msg_task_state_t*)  msg;
            struct pulsgen_msg_task_state_t* out = (struct pulsgen_msg_task_state_t*) &msg_buf;

            out->channels_mask = 0;

            // get task states
            for ( i = PULSGEN_CH_CNT; i--; )
            {
                if ( in->channels_mask & (1U << i) )
                {
                    if ( pulsgen_task_state(i) )    out->channels_mask |=  (1U << i);
                    else                            out->channels_mask &= ~(1U << i);
                }
            }

            // send an answer
            msg_send(type, (uint8_t*)&msg_buf, 4);

            break;
        }

        case PULSGEN_MSG_TASK_TOGGLES:
        {
            // setup messages view
            struct pulsgen_msg_task_toggles_t*  in = (struct pulsgen_msg_task_toggles_t*)  msg;
            struct pulsgen_msg_task_toggles_t* out = (struct pulsgen_msg_task_toggles_t*) &msg_buf;

            // get task toggles
            for ( i = PULSGEN_CH_CNT; i--; )
            {
                out->toggles[i] = in->toggles[i] ? pulsgen_task_toggles(i) : 0;
            }

            // send an answer
            msg_send(type, (uint8_t*)&msg_buf, 4*PULSGEN_CH_CNT);

            break;
        }

        default: return -1;
    }

    return 0;
}




/**
    @example mod_pulsgen.c

    <b>Usage example 1</b>: enable infinite PWM signal on GPIO pin PA3

    @code
        #include <stdint.h>
        #include "mod_gpio.h"
        #include "mod_pulsgen.h"

        int main(void)
        {
            // module init
            pulsgen_module_init();

            // use GPIO pin PA3 for the channel 0 output
            pulsgen_pin_setup(0, PA, 3, 0);

            // enable infinite PWM signal on the channel 0
            // PWM frequency = 20 kHz, duty cycle = 50%
            pulsgen_task_setup(0, 50, 0, 50, 0);

            // main loop
            for(;;)
            {
                // real update of channel states
                pulsgen_module_base_thread();
                // real update of pin states
                gpio_module_base_thread();
            }

            return 0;
        }
    @endcode

    <b>Usage example 2</b>: output of STEP/DIR signal

    @code
        #include <stdint.h>
        #include "mod_gpio.h"
        #include "mod_pulsgen.h"

        #define STEP_CHANNEL 0
        #define DIR_CHANNEL 1

        int main(void)
        {
            // uses to switch between DIR an STEP output
            uint8_t dir_output = 0; // 0 = STEP output, 1 = DIR output

            // module init
            pulsgen_module_init();

            // use GPIO pin PA3 for the STEP output on the channel 0
            pulsgen_pin_setup(STEP_CHANNEL, PA, 3, 0);

            // use GPIO pin PA5 for the DIR output on the channel 1
            pulsgen_pin_setup(DIR_CHANNEL, PA, 5, 0);

            // main loop
            for(;;)
            {
                if // if both channels aren't busy
                (
                    ! pulsgen_task_state(STEP_CHANNEL) &&
                    ! pulsgen_task_state(DIR_CHANNEL)
                )
                {
                    if ( dir_output ) // if it's time to make a DIR change
                    {
                        // make a DIR change with 1 kHz rate and 50% duty cycle
                        pulsgen_task_setup(DIR_CHANNEL, 1000, 1, 50, 0);
                        dir_output = 0;
                    }
                    else // if it's time to make a STEP output
                    {
                        // start output of 1000 steps with 20 kHz rate and 50% duty cycle
                        pulsgen_task_setup(STEP_CHANNEL, 50, 2000, 50, 0);
                        dir_output = 1;
                    }
                }

                // real update of channel states
                pulsgen_module_base_thread();
                // real update of pin states
                gpio_module_base_thread();
            }

            return 0;
        }
    @endcode
*/
