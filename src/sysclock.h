#ifndef __is_included__eb42c25c_df0f_11eb_ba7e_b499badf00a1
#define __is_included__eb42c25c_df0f_11eb_ba7e_b499badf00a1 1

/*@
        assigns TCCR0B, TCNT0, TCCR0A, TIFR0, TIMSK0, TCCR0B;

        ensures TCCR0B == 0x00;
        ensures TCNT0 == 0x00;
        ensures TCCR0A == 0x00;
        ensures TIFR0 == 0x01;
        ensures TIMSK0 == 0x01;
        ensures TCCR0B == 0x03;
*/
void sysclockInit();

/*@
        requires \valid(&SREG);
        assigns SREG;
*/
unsigned long int micros();

/*@
        requires millisecs >= 0;
        requires \valid(&SREG);
        assigns SREG;
*/
void delay(unsigned long millisecs);

#endif /* __is_included__eb42c25c_df0f_11eb_ba7e_b499badf00a1 */
