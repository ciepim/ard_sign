// Change this to be at least as long as your pixel string (too long will work fine, just be a little slower)

#define PIXELS 226  // Number of pixels in the string. I am using 4 meters of 96LED/M

// These values depend on which pins your 8 strings are connected to and what board you are using 
// More info on how to find these at http://www.arduino.cc/en/Reference/PortManipulation

// PORTD controls Digital Pins 0-7 on the Uno

// You'll need to look up the port/bit combination for other boards. 

// Note that you could also include the DigitalWriteFast header file to not need to to this lookup.

#define PIXEL_PORT  PORTD  // Port of the pin the pixels are connected to
#define PIXEL_DDR   DDRD   // Port of the pin the pixels are connected to


static const uint8_t onBits=0b11111110;   // Bit pattern to write to port to turn on all pins connected to LED strips. 
                                          // If you do not want to use all 8 pins, you can mask off the ones you don't want
                                          // Note that these will still get 0 written to them when we send pixels
                                          // TODO: If we have time, we could even add a variable that will and/or into the bits before writing to the port to support any combination of bits/values                                  

// These are the timing constraints taken mostly from 
// imperically measuring the output from the Adafruit library strandtest program

// Note that some of these defined values are for refernce only - the actual timing is determinted by the hard code.

#define T1H  814    // Width of a 1 bit in ns - 13 cycles
#define T1L  438    // Width of a 1 bit in ns -  7 cycles

#define T0H  312    // Width of a 0 bit in ns -  5 cycles
#define T0L  936    // Width of a 0 bit in ns - 15 cycles 

// Phase #1 - Always 1  - 5 cycles
// Phase #2 - Data part - 8 cycles
// Phase #3 - Always 0  - 7 cycles

#define RES 50000   // Width of the low gap between bits to cause a frame to latch

// Here are some convience defines for using nanoseconds specs to generate actual CPU delays

#define NS_PER_SEC (1000000000L)          // Note that this has to be SIGNED since we want to be able to check for negative values of derivatives

#define CYCLES_PER_SEC (F_CPU)

#define NS_PER_CYCLE ( NS_PER_SEC / CYCLES_PER_SEC )

#define NS_TO_CYCLES(n) ( (n) / NS_PER_CYCLE )


// Sends a full 8 bits down all the pins, represening a single color of 1 pixel
// We walk though the 8 bits in colorbyte one at a time. If the bit is 1 then we send the 8 bits of row out. Otherwise we send 0. 
// We send onBits at the first phase of the signal generation. We could just send 0xff, but that mught enable pull-ups on pins that we are not using. 

/// Unforntunately we have to drop to ASM for this so we can interleave the computaions durring the delays, otherwise things get too slow.

// OnBits is the mask of which bits are connected to strips. We pass it on so that we
// do not turn on unused pins becuase this would enable the pullup. Also, hopefully passing this
// will cause the compiler to allocate a Register for it and avoid a reload every pass.

static inline void sendBitx8(  const uint8_t row , const uint8_t colorbyte , const uint8_t onBits ) {  
              
    asm volatile (


      "L_%=: \n\r"  
      
      "out %[port], %[onBits] \n\t"                 // (1 cycles) - send either T0H or the first part of T1H. Onbits is a mask of which bits have strings attached.

      // Next determine if we are going to be sending 1s or 0s based on the current bit in the color....
      
      "mov r0, %[bitwalker] \n\t"                   // (1 cycles) 
      "and r0, %[colorbyte] \n\t"                   // (1 cycles)  - is the current bit in the color byte set?
      "breq OFF_%= \n\t"                            // (1 cycles) - bit in color is 0, then send full zero row (takes 2 cycles if branch taken, count the extra 1 on the target line)

      // If we get here, then we want to send a 1 for every row that has an ON dot...
      "nop \n\t  "                                  // (1 cycles) 
      "out %[port], %[row]   \n\t"                  // (1 cycles) - set the output bits to [row] This is phase for T0H-T1H.
                                                    // ==========
                                                    // (5 cycles) - T0H (Phase #1)


      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t "                                   // (1 cycles) 

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (8 cycles) - Phase #2
                                                    
      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t \n\t "                             // (1 cycles) - When added to the 5 cycles in S:, we gte the 7 cycles of T1L
            
      "jmp L_%= \n\t"                              // (3 cycles) 
                                                   // (1 cycles) - The OUT on the next pass of the loop
                                                   // ==========
                                                   // (7 cycles) - T1L
                                                   
                                                          
      "OFF_%=: \n\r"                                // (1 cycles)    Note that we land here becuase of breq, which takes takes 2 cycles

      "out %[port], __zero_reg__ \n\t"              // (1 cycles) - set the output bits to 0x00 based on the bit in colorbyte. This is phase for T0H-T1H
                                                    // ==========
                                                    // (5 cycles) - T0H

      "ror %[bitwalker] \n\t"                      // (1 cycles) - get ready for next pass. On last pass, the bit will end up in C flag
                  
      "brcs DONE_%= \n\t"                          // (1 cycles) Exit if carry bit is set as a result of us walking all 8 bits. We assume that the process around us will tak long enough to cover the phase 3 delay

      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles) 
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t nop \n\t "                          // (2 cycles)             
      "nop \n\t "                                   // (1 cycles)             
            
      "jmp L_%= \n\t"                               // (3 cycles) 
                                                    // (1 cycles) - The OUT on the next pass of the loop      
                                                    // ==========
                                                    //(15 cycles) - T0L 
      
            
      "DONE_%=: \n\t"

      // Don't need an explicit delay here since the overhead that follows will always be long enough
    
      ::
      [port]    "I" (_SFR_IO_ADDR(PIXEL_PORT)),
      [row]   "d" (row),
      [onBits]   "d" (onBits),
      [colorbyte]   "d" (colorbyte ),     // Phase 2 of the signal where the actual data bits show up.                
      [bitwalker] "r" (0x80)                      // Alocate a register to hold a bit that we will walk down though the color byte

    );
                                  
    // Note that the inter-bit gap can be as long as you want as long as it doesn't exceed the reset timeout (which is A long time)
    
} 


// Just wait long enough without sending any bots to cause the pixels to latch and display the last sent frame

void show() {
  delayMicroseconds( (RES / 1000UL) + 1);       // Round up since the delay must be _at_least_ this long (too short might not work, too long not a problem)
}


// Send 3 bytes of color data (R,G,B) for a signle pixel down all the connected stringsat the same time
// A 1 bit in "row" means send the color, a 0 bit means send black. 

static inline void sendRowRGB( uint8_t row ,  uint8_t r,  uint8_t g,  uint8_t b ) {

  sendBitx8( row , g , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , r , onBits);    // WS2812 takes colors in GRB order
  sendBitx8( row , b , onBits);    // WS2812 takes colors in GRB order
  
}

// Turn off all pixels

static inline void clear() {

  cli();
  for( unsigned int i=0; i< PIXELS; i++ ) {

    sendRowRGB( 0 , 0 , 0 , 0 );
  }
  sei();
  show(); 
}

// This nice 5x7 font from here...
// http://sunge.awardspace.com/glcd-sd/node4.html

// Font details:
// 1) Each char is fixed 5x7 pixels. 
// 2) Each byte is one column.
// 3) Columns are left to right order, leftmost byte is leftmost column of pixels.
// 4) Each column is 8 bits high.
// 5) Bit #7 is top line of char, Bit #1 is bottom.
// 6) Bit #0 is always 0, becuase this pin is used as serial input and setting to 1 would enable the pull-up.

// defines ascii characters 0x20-0x7F (32-127)
// PROGMEM after variable name as per https://www.arduino.cc/en/Reference/PROGMEM

#define FONT_WIDTH 5      
#define INTERCHAR_SPACE 1
#define ASCII_OFFSET 0x20    // ASSCI code of 1st char in font array

const uint8_t Font5x7[] PROGMEM = {
0xff,0xff,0xff,0xff,0xff,//
0xff,0xff,0x05,0xff,0xff,// !
0xff,0x1f,0xff,0x1f,0xff,// "
0xd7,0x01,0xd7,0x01,0xd7,// #
0xdb,0xab,0x01,0xab,0xb7,// $
0x3b,0x37,0xef,0xd9,0xb9,// %
0x93,0x6d,0x55,0xbb,0xf5,// &
0xff,0x5f,0x3f,0xff,0xff,// '
0xff,0xc7,0xbb,0x7d,0xff,// (
0xff,0x7d,0xbb,0xc7,0xff,// )
0xef,0xab,0xc7,0xab,0xef,// *
0xef,0xef,0x83,0xef,0xef,// +
0xff,0xf5,0xf3,0xff,0xff,// ,
0xef,0xef,0xef,0xef,0xef,// -
0xff,0xf9,0xf9,0xff,0xff,// .
0xfb,0xf7,0xef,0xdf,0xbf,// /
0x83,0x75,0x6d,0x5d,0x83,// 0
0xff,0xbd,0x01,0xfd,0xff,// 1
0xbd,0x79,0x75,0x6d,0x9d,// 2
0x7b,0x7d,0x5d,0x2d,0x73,// 3
0xe7,0xd7,0xb7,0x01,0xf7,// 4
0x1b,0x5d,0x5d,0x5d,0x63,// 5
0xc3,0xad,0x6d,0x6d,0xf3,// 6
0x7f,0x71,0x6f,0x5f,0x3f,// 7
0x93,0x6d,0x6d,0x6d,0x93,// 8
0x9f,0x6d,0x6d,0x6b,0x87,// 9
0xff,0x93,0x93,0xff,0xff,// :
0xff,0x95,0x93,0xff,0xff,// ;
0xff,0xef,0xd7,0xbb,0x7d,// <
0xd7,0xd7,0xd7,0xd7,0xd7,// =
0x7d,0xbb,0xd7,0xef,0xff,// >
0xbf,0x7f,0x75,0x6f,0x9f,// ?
0xb3,0x6d,0x61,0x7d,0x83,// @
0x81,0x77,0x77,0x77,0x81,// A
0x01,0x6d,0x6d,0x6d,0x93,// B
0x83,0x7d,0x7d,0x7d,0xbb,// C
0x01,0x7d,0x7d,0xbb,0xc7,// D
0x01,0x6d,0x6d,0x6d,0x7d,// E
0x01,0x6f,0x6f,0x7f,0x7f,// F
0x83,0x7d,0x7d,0x75,0xb3,// G
0x01,0xef,0xef,0xef,0x01,// H
0xff,0x7d,0x01,0x7d,0xff,// I
0xfb,0xfd,0x7d,0x03,0x7f,// J
0x01,0xef,0xd7,0xbb,0x7d,// K
0x01,0xfd,0xfd,0xfd,0xfd,// L
0x01,0xbf,0xdf,0xbf,0x01,// M
0x01,0xdf,0xef,0xf7,0x01,// N
0x83,0x7d,0x7d,0x7d,0x83,// O
0x01,0x6f,0x6f,0x6f,0x9f,// P
0x83,0x7d,0x75,0x7b,0x85,// Q
0x01,0x6f,0x67,0x6b,0x9d,// R
0x9d,0x6d,0x6d,0x6d,0x73,// S
0x7f,0x7f,0x01,0x7f,0x7f,// T
0x03,0xfd,0xfd,0xfd,0x03,// U
0x07,0xfb,0xfd,0xfb,0x07,// V
0x01,0xfb,0xe7,0xfb,0x01,// W
0x39,0xd7,0xef,0xd7,0x39,// X
0x3f,0xdf,0xe1,0xdf,0x3f,// Y
0x79,0x75,0x6d,0x5d,0x3d,// Z
0xff,0xff,0x01,0x7d,0x7d,// [
0xbf,0xdf,0xef,0xf7,0xfb,// (backslash)
0x7d,0x7d,0x01,0xff,0xff,// ]
0xdf,0xbf,0x7f,0xbf,0xdf,// ^
0xfd,0xfd,0xfd,0xfd,0xfd,// _
0xff,0x7f,0xbf,0xdf,0xff,// `
0xfb,0xd5,0xd5,0xd5,0xe1,// a
0x01,0xed,0xdd,0xdd,0xe3,// b
0xe3,0xdd,0xdd,0xdd,0xfb,// c
0xe3,0xdd,0xdd,0xed,0x01,// d
0xe3,0xd5,0xd5,0xd5,0xe7,// e
0xef,0x81,0x6f,0x7f,0xbf,// f
0xef,0xd7,0xd5,0xd5,0xc3,// g
0x01,0xef,0xdf,0xdf,0xe1,// h
0xff,0xdd,0x41,0xfd,0xff,// i
0xfb,0xfd,0xdd,0x43,0xff,// j
0xff,0x01,0xf7,0xeb,0xdd,// k
0xff,0x7d,0x01,0xfd,0xff,// l
0xc1,0xdf,0xe7,0xdf,0xe1,// m
0xc1,0xef,0xdf,0xdf,0xe1,// n
0xe3,0xdd,0xdd,0xdd,0xe3,// o
0xc1,0xd7,0xd7,0xd7,0xef,// p
0xef,0xd7,0xd7,0xe7,0xc1,// q
0xc1,0xef,0xdf,0xdf,0xef,// r
0xed,0xd5,0xd5,0xd5,0xfb,// s
0xdf,0x03,0xdd,0xfd,0xfb,// t
0xc3,0xfd,0xfd,0xfb,0xc1,// u
0xc7,0xfb,0xfd,0xfb,0xc7,// v
0xc3,0xfd,0xf3,0xfd,0xc3,// w
0xdd,0xeb,0xf7,0xeb,0xdd,// x
0xcf,0xf5,0xf5,0xf5,0xc3,// y
0xdd,0xd9,0xd5,0xcd,0xdd,// z
0xff,0xef,0x93,0x7d,0xff,// {
0xff,0xff,0x01,0xff,0xff,// |
0xff,0x7d,0x93,0xef,0xff,// }
0xef,0xef,0xab,0xc7,0xef,// ~
0xef,0xc7,0xab,0xef,0xef,// 
};

// Send the pixels to form the specified char, not including interchar space
// skip is the number of pixels to skip at the begining to enable sub-char smooth scrolling

// TODO: Subtract the offset from the char before starting the send sequence to save time if nessisary
// TODO: Also could pad the begining of the font table to aovid the offset subtraction at the cost of 20*8 bytes of progmem
// TODO: Could pad all chars out to 8 bytes wide to turn the the multiply by FONT_WIDTH into a shift 

static inline void sendChar( uint8_t c ,  uint8_t skip , uint8_t r,  uint8_t g,  uint8_t b ) {

  const uint8_t *charbase = Font5x7 + (( c -' ')* FONT_WIDTH ) ; 

  uint8_t col=FONT_WIDTH; 

  while (skip--) {
      charbase++;
      col--;    
  }
  
  while (col--) {
      sendRowRGB( pgm_read_byte_near( charbase++ ) , r , g , b );
  }    

  // TODO: FLexible interchar spacing

  sendRowRGB( 0 , r , g , b );    // Interchar space
  
}


// Show the passed string. The last letter of the string will be in the rightmost pixels of the display.
// Skip is how many cols of the 1st char to skip for smooth scrolling

static inline void sendString( const char *s , uint8_t skip ,  const uint8_t r,  const uint8_t g,  const uint8_t b ) {

  unsigned int l=PIXELS/(FONT_WIDTH+INTERCHAR_SPACE); 

  sendChar( *s , skip ,  r , g , b );   // First char is special case becuase it can be stepped for smooth scrolling
  
  while ( *(++s) && l--) {

    sendChar( *s , 0,  r , g , b );

  }
}

// A nice arcade font from...
// http://jared.geek.nz/2014/jan/custom-fonts-for-microcontrollers

#define ALTFONT_WIDTH 8

const uint8_t altfont[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,//  
  0x06,0x06,0x30,0x30,0x60,0xc0,0xc0,0x00,// !
  0xe0,0xe0,0x00,0xe0,0xe0,0x00,0x00,0x00,// "
  0x28,0xfe,0xfe,0x28,0xfe,0xfe,0x28,0x00,// #
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// $
  0xc6,0xce,0x1c,0x38,0x70,0xe6,0xc6,0x00,// %
  0xfe,0xfe,0xd6,0xc6,0x16,0x1e,0x1e,0x00,// &
  0xe0,0xe0,0x00,0x00,0x00,0x00,0x00,0x00,// '
  0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,// (
  0x00,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// )
  0x6c,0x10,0xfe,0xfe,0xfe,0x10,0x6c,0x00,// *
  0x10,0x10,0x7c,0x10,0x10,0x00,0x00,0x00,// +
  0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00,// ,
  0x10,0x10,0x10,0x10,0x10,0x00,0x00,0x00,// -
  0x06,0x06,0x00,0x00,0x00,0x00,0x00,0x00,// .
  0x0e,0x38,0xe0,0x00,0x00,0x00,0x00,0x00,// /
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// 0
  0x06,0x66,0x66,0xfe,0xfe,0x06,0x06,0x00,// 1
  0xde,0xde,0xd6,0xd6,0xd6,0xf6,0xf6,0x00,// 2
  0xc6,0xc6,0xd6,0xd6,0xd6,0xfe,0xfe,0x00,// 3
  0xf8,0xf8,0x18,0x18,0x18,0x7e,0x7e,0x00,// 4
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// 5
  0xfe,0xfe,0x36,0x36,0x36,0x3e,0x3e,0x00,// 6
  0xc2,0xc6,0xce,0xdc,0xf8,0xf0,0xe0,0x00,// 7
  0xfe,0xfe,0xd6,0xd6,0xd6,0xfe,0xfe,0x00,// 8
  0xf8,0xf8,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// 9
  0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,// :
  0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00,// ;
  0x10,0x28,0x44,0x44,0x00,0x00,0x00,0x00,// <
  0x28,0x28,0x28,0x28,0x28,0x00,0x00,0x00,// =
  0x44,0x44,0x28,0x10,0x00,0x00,0x00,0x00,// >
  0xc0,0xc0,0xda,0xda,0xd0,0xf0,0xf0,0x00,// ?
  0xfe,0xfe,0xc6,0xf6,0xd6,0xf6,0xf6,0x00,// @
  0xfe,0xfe,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// A
  0xfe,0xfe,0xd6,0xd6,0xf6,0x7e,0x3e,0x00,// B
  0xfe,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,// C
  0xfe,0xfe,0xc6,0xc6,0xe6,0x7e,0x3e,0x00,// D
  0xfe,0xfe,0xd6,0xd6,0xd6,0xd6,0xd6,0x00,// E
  0xfe,0xfe,0xd0,0xd0,0xd0,0xc0,0xc0,0x00,// F
  0xfe,0xfe,0xc6,0xc6,0xd6,0xde,0xde,0x00,// G
  0xfe,0xfe,0x18,0x18,0x18,0xfe,0xfe,0x00,// H
  0xc6,0xc6,0xfe,0xfe,0xc6,0xc6,0xc6,0x00,// I
  0x06,0x06,0x06,0x06,0x06,0xfe,0xfc,0x00,// J
  0xfe,0xfe,0x18,0x18,0x78,0xfe,0x9e,0x00,// K
  0xfe,0xfe,0x06,0x06,0x06,0x06,0x06,0x00,// L
  0xfe,0xfe,0xc0,0x60,0xc0,0xfe,0xfe,0x00,// M
  0xfe,0xfe,0x70,0x38,0x1c,0xfe,0xfe,0x00,// N
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// O
  0xfe,0xfe,0xd8,0xd8,0xd8,0xf8,0xf8,0x00,// P
  0xfe,0xfe,0xc6,0xce,0xce,0xfe,0xfe,0x00,// Q
  0xfe,0xfe,0xd8,0xdc,0xde,0xfe,0xfa,0x00,// R
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// S
  0xc0,0xc0,0xfe,0xfe,0xc0,0xc0,0xc0,0x00,// T
  0xfe,0xfe,0x06,0x06,0x06,0xfe,0xfe,0x00,// U
  0xf8,0xfc,0x0e,0x06,0x0e,0xfc,0xf8,0x00,// V
  0xfc,0xfe,0x06,0x0c,0x06,0xfe,0xfc,0x00,// W
  0xee,0xfe,0x38,0x10,0x38,0xfe,0xee,0x00,// X
  0xe0,0xf0,0x3e,0x1e,0x3e,0xf0,0xe0,0x00,// Y
  0xce,0xde,0xd6,0xd6,0xd6,0xf6,0xe6,0x00,// Z
  0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,// [
  0xe0,0x38,0x0e,0x00,0x00,0x00,0x00,0x00,// \
  0x00,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// ]
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// ^
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,// _
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// `
  0xfe,0xfe,0xd8,0xd8,0xd8,0xfe,0xfe,0x00,// a
  0xfe,0xfe,0xd6,0xd6,0xf6,0x7e,0x3e,0x00,// b
  0xfe,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,// c
  0xfe,0xfe,0xc6,0xc6,0xe6,0x7e,0x3e,0x00,// d
  0xfe,0xfe,0xd6,0xd6,0xd6,0xd6,0xd6,0x00,// e
  0xfe,0xfe,0xd0,0xd0,0xd0,0xc0,0xc0,0x00,// f
  0xfe,0xfe,0xc6,0xc6,0xd6,0xde,0xde,0x00,// g
  0xfe,0xfe,0x18,0x18,0x18,0xfe,0xfe,0x00,// h
  0xc6,0xc6,0xfe,0xfe,0xc6,0xc6,0xc6,0x00,// i
  0x06,0x06,0x06,0x06,0x06,0xfe,0xfc,0x00,// j
  0xfe,0xfe,0x18,0x18,0x78,0xfe,0x9e,0x00,// k
  0xfe,0xfe,0x06,0x06,0x06,0x06,0x06,0x00,// l
  0xfe,0xfe,0xc0,0x60,0xc0,0xfe,0xfe,0x00,// m
  0xfe,0xfe,0x70,0x38,0x1c,0xfe,0xfe,0x00,// n
  0xfe,0xfe,0xc6,0xc6,0xc6,0xfe,0xfe,0x00,// o
  0xfe,0xfe,0xd8,0xd8,0xd8,0xf8,0xf8,0x00,// p
  0xfe,0xfe,0xc6,0xce,0xce,0xfe,0xfe,0x00,// q
  0xfe,0xfe,0xd8,0xdc,0xde,0xfe,0xfa,0x00,// r
  0xf6,0xf6,0xd6,0xd6,0xd6,0xde,0xde,0x00,// s
  0xc0,0xc0,0xfe,0xfe,0xc0,0xc0,0xc0,0x00,// t
  0xfe,0xfe,0x06,0x06,0x06,0xfe,0xfe,0x00,// u
  0xf8,0xfc,0x0e,0x06,0x0e,0xfc,0xf8,0x00,// v
  0xfc,0xfe,0x06,0x0c,0x06,0xfe,0xfc,0x00,// w
  0xee,0xfe,0x38,0x10,0x38,0xfe,0xee,0x00,// x
  0xe0,0xf0,0x3e,0x1e,0x3e,0xf0,0xe0,0x00,// y
  0xce,0xde,0xd6,0xd6,0xd6,0xf6,0xe6,0x00,// z
  0x38,0xfe,0xfe,0x00,0x00,0x00,0x00,0x00,// {
  0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,// |
  0x00,0xfe,0xfe,0x38,0x00,0x00,0x00,0x00,// }
  0x00,0xfe,0x02,0xfe,0x00,0x00,0x00,0x00,// ~
};


// Keep track of where we are in the color cycle between chars
static uint8_t altbright =0; 

// Send a char with a column-based color cycle
static inline void sendCharAlt( uint8_t c ) {

  const uint8_t *charbase = altfont + (( c -' ')* ALTFONT_WIDTH) ; 

  uint8_t col=ALTFONT_WIDTH; 
  
  while (col--) {

      sendRowRGB(  pgm_read_byte_near( charbase++ ) , altbright , 0 , 0x80  );
   
      altbright+=10;
  }

  sendRowRGB( 0 ,0, 0 , 0 );
  altbright+=10;

}

// Show the passed string with the arcade font and a nice vertical color cycle effect

static inline void sendStringAlt( const char *s  ) {

  unsigned int l=PIXELS/(ALTFONT_WIDTH+INTERCHAR_SPACE); 

  while ( l--) {

    char c;  

    c =   *s++;

    if (!c) break;
    
    sendCharAlt( c  );

  }
}



// Set the specified pins up as digital out

void ledsetup() {

  PIXEL_DDR |= onBits;   // Set all used pins to output
    
}



void setup() {
    
  ledsetup();  
  
}


// https://learn.adafruit.com/led-tricks-gamma-correction/the-quick-fix

const uint8_t PROGMEM gamma[] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
    2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
    5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
   10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
   17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
   25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
   37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
   51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
   69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
   90, 92, 93, 95, 96, 98, 99,101,102,104,105,107,109,110,112,114,
  115,117,119,120,122,124,126,127,129,131,133,135,137,138,140,142,
  144,146,148,150,152,154,156,158,160,162,164,167,169,171,173,175,
  177,180,182,184,186,189,191,193,196,198,200,203,205,208,210,213,
  215,218,220,223,225,228,231,233,236,239,241,244,247,249,252,255 };

// Map 0-255 visual brightness to 0-255 LED brightness 
#define GAMMA(x) (pgm_read_byte(&gamma[x]))


void showcountdown() {

  // Start sequence.....

  const char *countdownstr = "            FIRE ROLLERBALL IN ";

  unsigned int count = 600; 

  clear();
  while (count>0) {

    count--;

    uint8_t digit1 = count/100;
    uint8_t digit2 = (count - (digit1*100)) / 10;
    uint8_t digit3 = (count - (digit1*100) - (digit2*10));

    uint8_t char1 = digit1 + '0';
    uint8_t char2 = digit2 + '0';
    uint8_t char3 = digit3 + '0';
    
    uint8_t brightness = GAMMA( ((count % 100) * 256)  / 100 );

    cli();
    sendString( countdownstr , 1 , brightness , brightness , brightness );      
 
    sendRowRGB( 0x00 , 0 , 0 , 0xff );
 
  //  sendChar( '0' , 0 , 0x80, 0 , 0 );
    
    sendChar( char1 , 0 , 0x80, 0 , 0 );
    sendChar( '.'   , 0 , 0x80, 0 , 0  );
    sendChar( char2 , 0 , 0x80, 0 , 0  );
    sendChar( char3 , 0 , 0x80, 0 , 0 );
    
    sei();
    show();
  }

  count = 100;

  // One last farewell blink

  while (count>0) {

    count--;

    
    uint8_t brightness = GAMMA( ((count % 100) * 256)  / 100 );

    cli();
    sendString( countdownstr , 1 , brightness , brightness , brightness );      

    sendRowRGB( 0x00 , 0 , 0 , 0xff );   // We need to quickly send a blank byte just to keep from missing our deadlne.
    sendChar( '0' , 0 , brightness, 0 , 0 );
    sendChar( '.' , 0 , brightness, 0 , 0 );
    sendChar( '0' , 0 , brightness, 0 , 0 );
    sendChar( '0' , 0 , brightness, 0 , 0 );
     
    
    sei();
    show();
  }
  
  
}


void showstarfield() {

  const uint8_t field = 40;       // Good size for a field, must be less than 256 so counters fit in a byte
 
  uint8_t sectors = (PIXELS / field);      // Repeating sectors makes for more stars and faster update

  for(unsigned int i=0; i<300;i++) {

    unsigned int r = random( PIXELS * 8 );   // Random slow, so grab one big number and we will break it down. 

    unsigned int x = r /8; 
    uint8_t y = r & 0x07;                // We use 7 rows
    uint8_t bitmask = (2<<y);           // Start at bit #1 since we enver use the bottom bit

    cli();    

      unsigned int l=x; 
    
      while (l--) {
           sendRowRGB( 0 , 0x00, 0x00, 0x00);          
      }
        
      sendRowRGB( bitmask , 0x40, 0x40, 0xff);  // Starlight blue

      l = PIXELS-x;
      
      while (l--) {
           sendRowRGB( 0 , 0x00, 0x00, 0x00);          
      }      
        
          

    sei();

   // show(); // Not needed - random is alwasy slow enough to trigger a reset
       
  }

}

static inline void sendIcon( const uint8_t *fontbase , uint8_t which, int8_t shift , uint8_t width , uint8_t r , uint8_t g , uint8_t b ) {

  const uint8_t *charbase = fontbase + (which*width);

  if (shift <0) {

        uint8_t shiftabs = -1 * shift;
  
        while (width--) {

          uint8_t row = pgm_read_byte_near( charbase++ );
    
          sendRowRGB(  row << shiftabs , r , g , b );
     
    }

  } else {


    
    while (width--) {
  
        sendRowRGB(  (pgm_read_byte_near( charbase++ ) >> shift) & onBits , r , g , b );
     
    }

  }

}


#define ENIMIES_WIDTH 12

const uint8_t enimies[] PROGMEM = {

  0x70,0xf4,0xfe,0xda,0xd8,0xf4,0xf4,0xd8,0xda,0xfe,0xf4,0x70, // Enimie 1 - open
  0x72,0xf2,0xf4,0xdc,0xd8,0xf4,0xf4,0xd8,0xdc,0xf4,0xf2,0x72, // Enimie 1 - close
  0x1c,0x30,0x7c,0xda,0x7a,0x78,0x7a,0xda,0x7c,0x30,0x1c,0x00, // Enimie 2 - open
  0xf0,0x3a,0x7c,0xd8,0x78,0x78,0x78,0xd8,0x7c,0x3a,0xf0,0x00, // Enimie 2 - closed
  0x92,0x54,0x10,0x82,0x44,0x00,0x00,0x44,0x82,0x10,0x54,0x92, // Explosion
};


void showinvaderwipe( uint8_t which , const char *pointsStr , uint8_t r , uint8_t g, uint8_t b) {

  clear();
  delay(500);

  for( uint8_t p = 0 ; p<strlen( pointsStr) ; p++ ) {

      cli();
      sendStringAlt( "                " );
      sendIcon( enimies , which , 0 , ENIMIES_WIDTH , r , g , b );
      for(uint8_t i=0; i<=p ;i++ ){        
        sendChar( *(pointsStr+i) , 0 ,r>>2 , g>>2 , b>>2 );     // Dim text slightly
      }
      sei();
      delay(100);
    
  }

  delay(1500);

  
}

void showinvaders() {
  
  showinvaderwipe(   3 ,  " = 20 POINTS" , 0x80 , 0x80 ,0x80 );
  showinvaderwipe(   1 ,  " = 10 POINTS" , 0x00 , 0xff ,0x00 );

  uint8_t acount = PIXELS/(ENIMIES_WIDTH+FONT_WIDTH);      // How many aliens do we have room for?  

  for( int8_t row = -4 ; row < 6 ; row++ ) {     // Walk down the rows

    //  Walk them 6 pixels per row 

    // ALternate direction on each row

    uint8_t s,e,d;

    if (row & 1) {

      s=1; e=8; d=1;
      
    } else {

      s=7; e=0; d=-1;
            
    }

    
    for( char p = s ; p!=e ; p +=d ) {
   
        // Now slowly move aliens
  
        // work our way though the alines moving each one to the left
        
      
          cli();
    
          // Start with margin

          uint8_t margin = p ;

          while (margin--) {
            sendRowRGB( 0 , 0x00 , 0x00 , 0x00 );
          }

          for( uint8_t l=0; l<acount ; l++ ) {

            sendIcon( enimies , p&1 , row, ENIMIES_WIDTH , 0xFF , 0xFF , 0xFF );
            sendChar( ' ' , 0 , 0x00 , 0x00 , 0x00 ) ; // No over crowding
            
          }
  
          sei();
          delay(70);
            
        }
   }
     // delay(200);            
  
}





void showallyourbase() {
  
  const char *allyourbase = "CAT: ALL YOUR BASE ARE BELONG TO US !!!" ;

  clear();
  for(unsigned int slide=10000; slide ; slide-=10 ) {
      altbright = (slide & 0xff);
      cli();      
      sendChar(' ' , 0 , 0 , 0 , 0 );
      sendStringAlt( allyourbase);
      sei();
      show();
  }
  
}

  

#define JAB_MAX_BRIGHTNESS 40
#define JAB_MIN_BRIGHTNESS 0x00
#define JAB_STEPS (JAB_MAX_BRIGHTNESS-JAB_MIN_BRIGHTNESS)

void showjabber() {

  const char *m = 

     " --- OPEN --- --- OPEN --- --- OPEN --- ---- OPEN ---"
     "     "
     "*** OPEN Mon - Fri 10.00 to 18.00 Sat 10.00 to 14.00 ***"
     "     "
     "Laura Bookkeeping & Administartion LTD ..."
     "     "
     "TAX Returns"
     "     "
     "Self Assessment"
     "     "
     "VAT   Payroll"
     "     "
     "Company formation"
     "     "
     "Corporation TAX"
     "     "
     "Accounts"
     "     "
     " --- OPEN --- --- OPEN --- --- OPEN --- ---- OPEN ---"
     "     "
     "*** OPEN Mon - Fri 10.00 to 18.00 Sat 10.00 to 14.00 ***"
     "     "
     "Constans INC Ltd ..."
     "     "
     "We are specialised in repairing Apple MacBooks, iMacs, iPhones and iPads."
     "     "
     "We repair all brands of PCs, SAT Navs and Laptops."
     "     "
     "Data recovery services."
     "     "
     "WWW.CONSTANS.UK"
     "     "
            ;

  // Text foreground color cycle effect
  uint8_t sector =0;
  uint8_t step=0;
    
  while (*m) {      

      if (step== JAB_STEPS) {
        step=0;
        sector++;
        if (sector==3) {
          sector=0;
        }
      } else {
        step++;
      }

      uint8_t rampup = JAB_MIN_BRIGHTNESS + step;
      uint8_t rampdown = JAB_MIN_BRIGHTNESS + (JAB_STEPS - step); 
      
      uint8_t r,g,b;
      
      switch( sector ) {
        case 0: 
            r=rampup;
            g=rampdown;
            b=JAB_MIN_BRIGHTNESS;
            break;
         case 1:
            r=rampdown;
            g=JAB_MIN_BRIGHTNESS;
            b=rampup;
            break;
         case 2:
            r=JAB_MIN_BRIGHTNESS;
            g=rampup;
            b=rampdown;
            break;
      
      };

      for( uint8_t step=0; step<FONT_WIDTH+INTERCHAR_SPACE  ; step++ ) {   // step though each column of the 1st char for smooth scrolling


       cli();

       sendString( m , step , r, g, b );
      
       sei();

       PORTB|=0x01;      
       delay(1);
       PORTB&=~0x01;
       _delay_ms(20);   // Slows down scrolling by pausing 10 milliseconds between horizontal steps
      }

    m++;

  }

}


void loop() {

  // showcountdown();
  // showallyourbase();
  // showstarfield();
  // showinvaders();
  showjabber();

  // TODO: Actually sample the state of the pullup on unused pins and OR it into the mask so we maintain the state.
  // Must do AFTER the cli(). 
  // TODO: Add offBits also to maintain the pullup state of unused pins. 
  
  return;  
}
