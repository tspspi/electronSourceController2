#ifndef __is_included__eb42c25c_df0f_11eb_ba7e_b499badf00a1
#define __is_included__eb42c25c_df0f_11eb_ba7e_b499badf00a1 1

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
