/*
*   Multichannel DMX512 PWM firmware
*   Authors: Jose Fernando Gomez Díaz
*            Roberto Salinas Rosich
*
*   Thanks to Nocturno from Micropic (www.micropic.es), whose DMX receiver based this code.
*
*   Based on DMX dimmer
*
*   This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
 */


#define _XTAL_FREQ 8000000

#include <xc.h>
#include "config.h"


/*Tri-State ports*/
#define IN   1
#define OUT  0

/*Output latches*/
#define FOG_PORT    LATCbits.LATC6
#define READY_PORT  LATCbits.LATC3

/*Input ports*/
#define BIT_1_PIN PORTAbits.RA2
#define BIT_2_PIN PORTCbits.RC0
#define BIT_3_PIN PORTCbits.RC1
#define BIT_4_PIN PORTCbits.RC2
#define BIT_5_PIN PORTBbits.RB4
#define BIT_6_PIN PORTBbits.RB5
#define BIT_7_PIN PORTBbits.RB6
#define BIT_8_PIN PORTBbits.RB7
#define BIT_9_PIN PORTCbits.RC7
#define READY_IN    PORTCbits.RC4

/*Tri state and pull-up registers*/
#define TRISA_LOAD 0b00000100
#define TRISB_LOAD 0b11110000
#define TRISC_LOAD 0b10110111

#define WPUA_LOAD 0b00000100
#define WPUB_LOAD 0b11110000
#define WPUC_LOAD 0b10000111

/*DMX safety timer*/
#define t0_treshold 32

/*Internal DMX parameters*/
#define TOTALCHANNELS       512
#define DMX_ESPERA_BYTE     0
#define DMX_ESPERA_BREAK    1
#define DMX_ESPERA_START    2
#define DMX_ESPERA_DATO     3
#define DMX_RECEPCION_DATOS 4


/*Function prototypes*/
inline void osc_init(void);
inline void port_init(void);
inline void uart_init(void);
inline void timer_init (void);
inline void software_init (void);
inline void interrupts_init (void);
inline int read_addr(void);
inline void update_outputs(void);

/*DMX Variables*/
unsigned char DMX_Estado = DMX_ESPERA_BREAK;    //FSM status
unsigned char DatoRX;                           //Recived data
int DMX_Indice = 0;                             //Loop variable for FrameDMX
int add0=0;                                     //Starting address
unsigned char ttl_counter;                      //TTL timeout counter
unsigned char fog_on;                           //Fog on

union  // Estructura para hacer una copia del registro RCSTA
   {
   unsigned char registro;
   struct {
     unsigned char RX9D:1;
     unsigned char OERR:1;
     unsigned char FERR:1;
     unsigned char ADDEN:1;
     unsigned char CREN:1;
     unsigned char SREN:1;
     unsigned char RX9:1;
     unsigned char SPEN:1;
           } bits ;
  }Copia_RCSTA;

void main(void)
{
    osc_init();
    port_init();
    uart_init();
    timer_init();
    software_init();
    interrupts_init();
    INTCONbits.GIE = 1;    //Global interrupts enable
    while(1)
    {
        add0=read_addr();         //Read DIP switch address    
        update_outputs();
   }
}

inline void osc_init(void)
{
        /*Oscillator configurations*/
     OSCCONbits.SPLLEN=0;   //Main PLL disabled
     OSCCONbits.IRCF=0b1110;  //8 MHz clock source
     OSCCONbits.SCS=0b11;     //Internal clock source
     while(!OSCSTATbits.HFIOFR);
}

inline void port_init(void)
{
        /*Port configurations*/
    TRISA  = TRISA_LOAD; //RA2 as key input
    WPUA   = WPUA_LOAD; //Pullup enabled
    ANSELA = 0;          //No analog inputs
    INLVLA = 0;          //TTL

    TRISB  = TRISB_LOAD; //RB4-7 as key input
    WPUB   = WPUB_LOAD; //Pullup enabled
    ANSELB = 0;          //No analog inputs
    INLVLB = 0;          //TTL

    TRISC  = TRISC_LOAD; //RC7 as key input, RC6 as out, RC5 as DMX in, RC4 as heating in, RC3 as Ready, RC2-0 As key input;
    WPUC   = WPUC_LOAD;
    ANSELC = 0;
    INLVLC = 0;          //TTL

    /*Custom values*/
    APFCON0bits.RXDTSEL=1;  // UART RX on RC5
    OPTION_REGbits.nWPUEN=0;//Pullups enabled
}

inline void uart_init(void)
{
        /*USART configurations*/
    TXSTAbits.BRGH=1;           // Alta velocidad seleccionada.
    BAUDCONbits.BRG16=1;        // Baudrate de 16 bits
    TXSTAbits.SYNC=0;           // Seleccionamos transmisión asíncrona
    SPBRG=7;                    // A 8MHz representa Baudios = 250KHz
    SPBRGH=0;
    RCSTAbits.RX9=1;            // Activada la recepción a 9 bits
    RCSTAbits.SREN=0;           // Desactivada la recepción de un sólo byte
    RCSTAbits.ADDEN=0;          // Desactivada la autodetección de dirección
    RCSTAbits.FERR=0;           // No hay error de frame
    RCSTAbits.OERR=0;           // No hay error de overrun
    RCSTAbits.SPEN=1;           // USART activada
    RCSTAbits.CREN=1;           // Recepción activada
}

inline void timer_init(void)
{
        /*Timer0 configurations*/
    OPTION_REGbits.PSA=0;       //Prescaler is asigned
    OPTION_REGbits.PS=7;        //256 Prescaler
    OPTION_REGbits.T0CS=0;      //Fosc/4
    TMR0=0;                     //Clear TTL timer
    ttl_counter=0;
}

inline void software_init(void)
{
    add0 = 0;                   //Clear DMX starting address
    fog_on = 0;                 //Disable_outputs
    READY_PORT = 0;
    FOG_PORT = 0;
}

inline void interrupts_init (void)
{

    /*Interrupts Configuration*/
    PIE1bits.RCIE=1;           //EUSART Receiving Interrupt Enable
    PIR1bits.RCIF=0;           //Clear EUSART interruption flag
    PIE1bits.ADIE = 0;         //Disable ADC interrupt
    INTCONbits.TMR0IE=1;       //Enable timer0 interrupts
    INTCONbits.TMR0IF=0;       //Clear timer0 interrupt
    INTCONbits.PEIE=1;         //Peripheral interrupts enable
}

/*for 18F29*/
inline int read_addr(void)
{
    int number;
    number=0;
    if (!BIT_1_PIN) number+=1;
    if (!BIT_2_PIN) number+=2;
    if (!BIT_3_PIN) number+=4;
    if (!BIT_4_PIN) number+=8;
    if (!BIT_5_PIN) number+=16;
    if (!BIT_6_PIN) number+=32;
    if (!BIT_7_PIN) number+=64;
    if (!BIT_8_PIN) number+=128;
    if (!BIT_9_PIN) number+=256;
    return (number);
}

inline void update_outputs(void)
{
    if (!READY_IN){
        FOG_PORT=0;
        READY_PORT=0;
    }
    else{
        READY_PORT = !fog_on;
        FOG_PORT=fog_on;
    }
}


void interrupt ISR(void)
{
  if(INTCONbits.T0IF){
      INTCONbits.T0IF=0;    //Clear flag
      ttl_counter++;
      if (ttl_counter >=t0_treshold){
        fog_on=0;
        INTCONbits.T0IE=0;  //Stop timer interrupts
        ttl_counter=0;   //Clear counter
      }
  }

  while (PIR1bits.RCIF) // ejecutamos mientras haya un dato pendiente de procesar
   {
     // Hacemos una copia del registro RCSTA porque sus bits cambian de valor
    // al leer RCREG y modificar CREN
    Copia_RCSTA.registro = RCSTA;

    // En RCREG está el dato que acaba de recibir la USART
    DatoRX = RCREG;

    // Si se reciben más de 3 bytes sin haberlos procesado, se produce un error
    // de Overrun. En este caso, se borra el error reiniciando CREN y dejamos
    // la interrupción preparada para procesar la siguiente trama DMX
    if (Copia_RCSTA.bits.OERR)
    {
      CREN=0;
      CREN=1;
      DMX_Estado = DMX_ESPERA_BYTE;
      return;
    }

    // Máquina de estados
    switch (DMX_Estado){
      case DMX_ESPERA_BYTE:   // si estamos en este estado y hay error FRAME
      // es que nos ha pillado en medio de un Byte. Hay que seguir esperando
      // hasta que desaparezca el error.
        if (!Copia_RCSTA.bits.FERR)
              // Ha llegado un byte. Ahora esperaremos la señal Break
          DMX_Estado = DMX_ESPERA_BREAK;
        break;


      case DMX_ESPERA_BREAK:   // estamos esperando la señal Break
        // Esta señal se identifica porque aparece el error de Frame
        if (Copia_RCSTA.bits.FERR)
          // Tras recibir el error de Break, hay que esperar un byte de valor 0
          if (!DatoRX)
            DMX_Estado = DMX_ESPERA_START;
        break;
      case DMX_ESPERA_START: // ya hemos recibido el Break y ahora hay que
          // esperar un Byte con valor 0, que será la señal de Start
        // Mientras tanto, si recibimos un error de Frame, hay que volver a
        // empezar para recibir la señal de comienzo de trama.
        if (Copia_RCSTA.bits.FERR)
            DMX_Estado = DMX_ESPERA_BYTE;
        else {
          if (!DatoRX)
          {
            // Llegados a este punto, ya hemos recibido el Byte Start=0
            // y comenzamos la trama de valores DMX.
            DMX_Indice = 0;
            DMX_Estado = DMX_RECEPCION_DATOS;
          } else
            // Si el dato recibido no es 0, volvemos a empezar
            DMX_Estado = DMX_ESPERA_BREAK;
        }
        break;
      case DMX_RECEPCION_DATOS:
        // En este estado estamos recibiendo la trama de datos DMX
        // Si se detecta un error de Frame es que ha habido un error y estamos
        // al principio
        if (Copia_RCSTA.bits.FERR)
          if (!DatoRX)
            DMX_Estado = DMX_ESPERA_START;
          else
            DMX_Estado = DMX_ESPERA_BYTE;
        else{
            if(DMX_Indice==add0){
                if(DatoRX>=140){
                    fog_on=1;
                }
                if(DatoRX<=120){
                   fog_on=0;
                   
                }

                TMR0=0;        //Reset timer
                ttl_counter=0;
                if(!INTCONbits.TMR0IE){      //If timer interrupts are disabled
                    INTCONbits.T0IE=1;      //enable them
                    INTCONbits.T0IF=0;
                }
            }
            DMX_Indice++;            
            if (DMX_Indice >= TOTALCHANNELS){
                DMX_Estado = DMX_ESPERA_BREAK;
            }
        }
        break;
   }
}

}