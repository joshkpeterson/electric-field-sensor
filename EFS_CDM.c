#include <msp430f2012.h>
#include <stdint.h>
#include <in430.h>

#define NUM_CLOCKS_9600_BAUD       (104)
#define NUM_SAMPLES                (256)
#define NUM_CHANNELS               (2)
#define NORMALIZATION_SHIFT        (7)
#define NUM_DIGITS_TO_SEND         (9)
#define DC_OFFSET_A                (0)
#define DC_OFFSET_B                (0)
#define TOGGLE                     (0xFFFF)

#define LFSR_START_A               (0x01u)
#define LFSR_START_B               (0xFFu)
#define LFSR_MASK                  (0xB8u)
#define LFSR_PERIOD                (255)
#define TOGGLE_MASK                (0x00)
#define LSB                        (BIT0)

static uint8_t lfsr[] = {LFSR_START_A, LFSR_START_B};
static int coefficient[] = {0, 0};
static uint8_t toggle_bits[] = {0x04, 0x40}; 

static unsigned char shiftedChar;
static int numBits = 0;
static long accumulator[]    = {0, 0};
static long low_pass_accum[] = {0, 0};

// State machine values
#define CHANNEL_A                   (0)
#define CHANNEL_B                   (1)

static int state;
static int sampleCount = 0;
static unsigned char buf[] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\n'};
static int index = 0;


void low_pass_filter() {
//  long temp_acc = accumulator[state]>>2;
//  long temp_low = low_pass_accum[state]>>2;
//  low_pass_accum[state] = temp_low + temp_low + temp_acc + temp_acc;
  low_pass_accum[state] = (low_pass_accum[state]>>1) + (accumulator[state]>>1);
}


uint8_t LFSR(uint8_t lfsr) {
  /* taps: 8 6 5 4; char. poly: x^8+x^6+x^5+x^4+1 */
  return (lfsr >> 1) ^ (-(lfsr & 1u) & 0xB8u);
}


void itoa(unsigned int i) {
  if (state == CHANNEL_A) {
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
//  itoa(low_pass_accum[state]);
  itoa(low_pass_accum[state]>>NORMALIZATION_SHIFT);
//  itoa(accumulator[state]);             //lowpass disabled for CNR
  TAR = 0;                                // reset TimerA counter
  P1DIR = BIT0;                           // set P1.0 out for transmit
  P1OUT = BIT0;                           // initialize high for idle value on wire
  CCTL0 |= CCIE;                          // CCRO interrupt enabled for transmit
  index = 0;                              // reinitialize local variables
}

void sample_mode() { 
  CCTL0 &= ~CCIE;                         // CCRO interrupt disabled for sampling
  P1DIR = BIT2 | BIT6;                    // set P1.2 and P1.6 out
  
  uint8_t toggle_mask = TOGGLE_MASK;
  int last_out;
  int this_out;
  int channel;
  for (channel = 0; channel < NUM_CHANNELS; channel++) {
    accumulator[channel] = 0;                   // reset accumulator for next round of sampling
    last_out = lfsr[channel]&LSB;               // capture the out bit for the last period
    lfsr[channel] = LFSR(lfsr[channel]);        // update the lfsr for this time step
    this_out = lfsr[channel]&LSB;               // capture the out bit for the this period
    if (last_out^this_out) { 
      toggle_mask |= toggle_bits[channel];      // OR in the toggle bits if there is an edge
    }
    coefficient[channel] = this_out - last_out; // (may be reversed) compute the accumulation coefficient
  }
  sampleCount = 1;                              // reset for next capture round
  P1OUT ^= toggle_mask;                         // toggle the transmit antennas
  ADC10CTL0 |= ADC10SC;                         // start conversion
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
  
  state = CHANNEL_A;                        // initialize state variable
  sample_mode();                           
  _BIS_SR(CPUOFF + GIE);                    // Enter LPM0

  
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
  	  case CHANNEL_A:
  	    state = CHANNEL_B;
  	    transmit_mode();
  	    break;
  	  case CHANNEL_B:
  	    state = CHANNEL_A;
  	    sample_mode();
  	} // end switch
  } // end else
} // end Timer_A


// ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR (void)
{
  int sample = ADC10MEM;

  int channel;
//  if (lfsr[0] != LFSR_START_A && lfsr[1] != LFSR_START_B) {
  if (sampleCount <= LFSR_PERIOD) {
    sampleCount++;
    for (channel = 0; channel < NUM_CHANNELS; channel++) {
      int difference = sample - (channel ? DC_OFFSET_B : DC_OFFSET_A);
      difference = (difference > 0 ? difference : 0);
      if (coefficient[channel] == 1) {
        accumulator[channel] += difference;
      } else if (coefficient[channel] == -1) {
        accumulator[channel] -= difference;
      }
    }
  }

//  if (lfsr[0] != LFSR_START_A && lfsr[1] != LFSR_START_B) {
  if (sampleCount <= LFSR_PERIOD) {
    uint8_t toggle_mask = TOGGLE_MASK;
    int last_out;
    int this_out;
    int channel;
    for (channel = 0; channel < NUM_CHANNELS; channel++) {
      last_out = lfsr[channel]&LSB;               // capture the out bit for the last period
      lfsr[channel] = LFSR(lfsr[channel]);        // update the lfsr for this time step
      this_out = lfsr[channel]&LSB;               // capture the out bit for the this period
      if (last_out^this_out) { 
        toggle_mask |= toggle_bits[channel];      // OR in the toggle bits if there is an edge
      }
      coefficient[channel] = this_out - last_out; // (may be reversed) compute the accumulation coefficient
    }
    P1OUT ^= toggle_mask;                         // toggle the transmit antennas
    ADC10CTL0 |= ADC10SC;                         // start conversion
  } else transmit_mode(); 		
} // end ADC10_ISR
