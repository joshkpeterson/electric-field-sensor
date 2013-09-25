#include <msp430f2012.h>
#include <stdint.h>
#include <in430.h>

#define NUM_CLOCKS_9600_BAUD       (104)
#define NUM_SAMPLES                (256)
#define LOG_NUM_SAMPLES            (7)
#define NUM_DIGITS_TO_SEND         (9)
#define DC_OFFSET_A                (0)
#define DC_OFFSET_B                (0)
#define TOGGLE                     (0xFFFF)

// State machine values
#define SAMPLE_A                   (0)
#define SAMPLE_B                   (1)
#define TRANSMIT_A                 (0)
#define TRANSMIT_B                 (1)

static int state;

static unsigned char shiftedChar;
static int numBits = 0;
static unsigned int accumulator[]    = {0, 0};
static unsigned int low_pass_accum[] = {0, 0};

static int sampleCount = 1;
static unsigned int odd_sample = 0;
static unsigned int even_sample = 0;

static unsigned char buf[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\n'};
static int index = 0;


void low_pass_filter() {
//  unsigned int temp_acc = accumulator[state]>>2;
//  unsigned int temp_low = low_pass_accum[state]>>2;
//  low_pass_accum[state] = temp_low + temp_acc + temp_acc + temp_acc;
  low_pass_accum[state] = (low_pass_accum[state]>>1) + (accumulator[state]>>1);
}

void itoa(unsigned int i) {
  if (state == SAMPLE_A) {
  	buf[0] = 'A';
  	buf[8] = ',';
  } else {
  	buf[0] = 'B';
  	buf[8] = '\n';
  }  	
  buf[2] = ' ';
  buf[3] = ' ';
  buf[4] = ' ';
  buf[5] = ' ';
  buf[6] = ' ';
  int digit = NUM_DIGITS_TO_SEND - 2;
  while (i > 0) {
    buf[digit--] = (i%10) + '0';
    i = i/10;
  }
}

void transmit_mode() {
  low_pass_filter();
  itoa(low_pass_accum[state]>>LOG_NUM_SAMPLES);
//  itoa(low_pass_accum[state]);
//  itoa(accumulator[state]);               //lowpass disabled for CNR
  TAR = 0;                                // reset TimerA counter
  P1DIR = BIT0;                           // set P1.0 out for transmit
  P1OUT = BIT0;                           // initialize high for idle value on wire
  CCTL0 |= CCIE;                          // CCRO interrupt enabled for transmit
  index = 0;                              // reinitialize local variables
}

void sample_mode() { 
  if (state == SAMPLE_A) {
    P1DIR = BIT2;
    P1OUT = BIT2;
  } else {
    P1DIR = BIT6;
    P1OUT = BIT6;
  }
  CCTL0 &= ~CCIE;                       // CCRO interrupt disabled for sampling
  accumulator[state] = 0;               // reset accumulator for next round of sampling
  sampleCount = 1;                      // reset for next capture round
  odd_sample = 0;
  even_sample = 0;
  if (state == SAMPLE_A) {
    P1OUT ^= BIT2;
  } else {
    P1OUT ^= BIT6;
  }
  ADC10CTL0 |= ADC10SC;
}


void main(void)
{
  WDTCTL = WDTPW + WDTHOLD;                 // Stop WDT
  CCTL0 &= ~CCIE;                           // CCRO interupt disabled
  CCR0 = NUM_CLOCKS_9600_BAUD;              // initialize for transmit
  TACTL = TASSEL_2 + MC_1;                  // SMCLK, up mode
  
  ADC10CTL0 = ADC10SHT_0 + ADC10ON + ADC10IE; // 4 clocks S&H, ADC10ON, interrupt enabled
  ADC10CTL1 = INCH_1;                       // input A1
  ADC10AE0 |= BIT1;                         // PA.1 ADC option select
  ADC10CTL0 |= ENC;
 
  DCOCTL = CALDCO_1MHZ;                     // set these two registers to the calibration
  BCSCTL1 = CALBC1_1MHZ;                    // constant values to get 1Mhz
  
  state = SAMPLE_A;                        // initialize state variable
  sample_mode();                           
  _BIS_SR(CPUOFF + GIE);                   // Enter LPM0
}


// Timer A0 interrupt service routine
#pragma vector=TIMERA0_VECTOR
__interrupt void Timer_A (void)
{
  if (index < NUM_DIGITS_TO_SEND) {
    if (numBits == 0) { // Start bit
      P1OUT = 0;
      shiftedChar = buf[index];
      numBits++;
    } else if (numBits == 9) { // Stop bit
      P1OUT = BIT0;
      numBits = 0;
      index++;
    } else { // data bits
      P1OUT = shiftedChar & BIT0;
      shiftedChar >>= 1;  
      numBits++;
    }
  } else {
  	// State machine updates
  	switch (state) {
  	  case TRANSMIT_A:
  	    state = TRANSMIT_B;
  	    transmit_mode();
  	    break;
  	  case TRANSMIT_B:
  	    state = SAMPLE_A;
  	    sample_mode();
  	} // end switch
  } // end else
} // end Timer_A


// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR (void)
{
  // Accumulate +/- values and then switch to transmit mode
  odd_sample = even_sample;
  even_sample = ADC10MEM;                    // read sampled value
  
  if (sampleCount <= NUM_SAMPLES) {
  	if (!(sampleCount & 1)) {                // if this is an even iteration
  	  int difference = odd_sample - even_sample - (state ? DC_OFFSET_B : DC_OFFSET_A);
  	  if (difference >= 0) {
        accumulator[state] += difference; // otherwise add the difference
  	  }
  	}
  	sampleCount++;
  if (state == SAMPLE_A) {
    P1OUT ^= BIT2;
  } else {
    P1OUT ^= BIT6;
  }
    ADC10CTL0 |= ADC10SC;           // start conversion
  } else {                          // already accumulated NUM_SAMPLES, now transmit
  	// State machine updates
  	switch (state) {
  	  case SAMPLE_A:
  	    state = SAMPLE_B;
  	    sample_mode();
  	    break;
  	  case SAMPLE_B:
  	    state = TRANSMIT_A;
  	    transmit_mode();
  	} // end switch
  } // end else
} // end ADC10_ISR
