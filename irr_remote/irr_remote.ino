/*
Sainsmart LCD Shield for Arduino
 
 None   - 0
 Select - 1
 Left   - 2
 Up     - 3
 Down   - 4
 Right  - 5
 
 */

#include <LiquidCrystal.h>
#include <LCDKeypad.h>
#include <DFR_Key.h>

#include <Time.h>  

#include <SPI.h>
#include <Mirf.h>
#include <nRF24L01.h>
#include <MirfHardwareSpiDriver.h>

#define RADIO_CE             2
#define RADIO_CSN            3
#define BACKLIGHT_PIN        10

LCDKeypad lcd;

byte c_up[8] = {
  B00100,
  B01110,
  B10101,
  B00100,
  B00100,
  B00100,
  B00100,
  B00100,
};

byte c_down[8] = {
  B00100,
  B00100,
  B00100,
  B00100,
  B00100,
  B10101,
  B01110,
  B00100,
};

byte c_select[8] = {
  B00000,
  B01110,
  B11111,
  B11111,
  B11111,
  B11111,
  B01110,
  B00000,
};

struct radio_receive_packet{
  unsigned int humidy;
  long unsigned int cur_time;
  long unsigned int last_irr;
  int cur_task;      // 0 if waiting
  long unsigned int next_task_time;
  int next_task_type;
  unsigned int bat_level;
}
RRP;

struct radio_send_packet{
  long unsigned int time;  // 0 if not set
  int task;  // 0 if none
}
RSP;  

const long radio_send_packet_sz = sizeof(radio_send_packet);
const long radio_receive_packet_sz = sizeof(radio_receive_packet);

// ********************************************
// Работа с радио
// ********************************************

void setup_Mirf(void)
{
  /*
   * Set the SPI Driver.
   */

  Mirf.cePin = 7;
  Mirf.csnPin = 10;
  Mirf.spi = &MirfHardwareSpi;

  /*
   * Setup pins / SPI.
   */

  Mirf.init();

  /*
   * Configure reciving address.
   */

  Mirf.setRADDR((byte *)"irrig");

  /*
   * Set the payload length to sizeof(unsigned long) the
   * return type of millis().
   *
   * NB: payload on client and server must be the same.
   */

  Mirf.payload = radio_receive_packet_sz;

  /*
   * Write channel and payload config then power up reciver.
   */

  Mirf.config();

  Serial.println("Radio ready"); 
}

bool radio_receive(void* packet)
{

  byte data[radio_receive_packet_sz];

  if(!Mirf.isSending() && Mirf.dataReady()){
    Serial.println("Got packet");

    /*
     * Get load the packet into the buffer.
     */

    Mirf.getData(data);

    memcpy(packet,&data,radio_receive_packet_sz);
    return true;
  }
  else return false;

}

void radio_send(void* packet)
{

  Mirf.payload = radio_send_packet_sz;
  Mirf.config();

  Mirf.setTADDR((byte *)"irr_rem");
  
  Serial.println("Start sending");
  backlight(false);

  Mirf.send((byte *)packet);

  while(Mirf.isSending()){
  }
  
  backlight(true);
  Serial.println("Finished sending");

  Mirf.payload = radio_receive_packet_sz;
  Mirf.config();
}

// ********************************************
// Работа с экраном и кнопками
// ********************************************  

void backlight(bool state)
{
  pinMode(BACKLIGHT_PIN, OUTPUT);
  if (state) {
    digitalWrite(BACKLIGHT_PIN, HIGH);
  } 
  else {
    digitalWrite(BACKLIGHT_PIN, LOW);
  }
}

bool waitButton(int buttonPressed)
{
  return !(buttonPressed=lcd.button()==KEYPAD_NONE);
}

void waitReleaseButton()
{
  delay(50);
  while(lcd.button()!=KEYPAD_NONE)
  {
  }
  delay(50);
}

// ********************************************
// Процедуры печати диагностических сообщений
// ********************************************

void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
  Serial.print('0');
  Serial.print(digits);
}

void lcd_printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  lcd.print(":");
  if(digits < 10)
  lcd.print('0');
  lcd.print(digits);
}

void lcd_print_dt(time_t dt)
{
  lcd.print(day(dt), DEC);
  lcd.print('/');
  lcd.print(month(dt), DEC);
  lcd.print('/');
  lcd.print(year(dt), DEC);
  lcd.print(' ');
  lcd.print(hour(dt), DEC);
  lcd_printDigits(minute(dt));
  lcd_printDigits(second(dt));
}

void print_dt(time_t dt)
{
  Serial.print(day(dt), DEC);
  Serial.print('/');
  Serial.print(month(dt), DEC);
  Serial.print('/');
  Serial.print(year(dt), DEC);
  Serial.print(' ');
  Serial.print(hour(dt));
  printDigits(minute(dt));
  printDigits(second(dt));
  Serial.println();
}

// ********************************************
// Основная программа
// ********************************************                

void setup() 
{ 
  // LCD setup
//  lcd.createChar(1,c_select);
//  lcd.createChar(2,c_up);
//  lcd.createChar(3,c_down);
  lcd.begin(16, 2);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IRR REMOTE v0.1");
  delay(1000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Establishing");
  lcd.setCursor(0, 1);
  lcd.print("communication");

  // Init communications
  Serial.begin(57600);
  Serial.println("Irrigator remote control started");  

  // Init Mirf
  setup_Mirf();
  
  // First radio
  RSP.time = 0;
  RSP.task = 0;
  Serial.println("Sending initial packet...");
  radio_send(&RSP);
  
}

void loop() 
{ 
  int buttonPressed;
  static int state = 0, next_radio = millis() + 5000;
  static bool state_changed = false;
  static time_t last_time;
  
  if (state==0 && !state_changed && timeStatus() != timeNotSet) { // вывести текущее время
    time_t now_t = now();
    Serial.println(now_t);
    Serial.println(last_time);
    if (now_t>last_time) {
      lcd.setCursor(0,1);
      lcd_print_dt(now_t);
      print_dt(now_t);
      last_time = now_t;
    }
  }
  
  if (millis() > next_radio) {
    Serial.println("Sending...");
    radio_send(&RSP);
    next_radio = millis() + 5000;
    RSP.time = 0;  // Исправить. Обнулять только после подтверждения доставки или таймаута
    RSP.task = 0;
  }

  if (radio_receive(&RRP)){ 
    //    обработать принятый пакет
    Serial.println("Trying to adjust RTC");
    setTime(RRP.cur_time);

    switch(state)
    {
    case 0:
      // cur_time + cur_task
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Cur_task:");
      lcd.print(RRP.cur_task,DEC);
      lcd.print(" Time:");
      lcd.setCursor(0,1);
      lcd_print_dt(RRP.cur_time);
      break;
    case 1:
      // last_irr
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Last irr:");
      lcd.setCursor(0,1);
      lcd_print_dt(RRP.last_irr);
      break;
    case 2:
      // next_task_time, next_task_type
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("Next_task:");
      lcd.print(RRP.next_task_type,DEC);
      //lcd.print(" Time:");
      lcd.setCursor(0,1);
      lcd_print_dt(RRP.next_task_time);      
      break;
      //default:
    }
  }

  if (waitButton(buttonPressed)) {
    waitReleaseButton();

    //  do
    //  {
    //    buttonPressed=waitButton();
    //  }
    //  while(!(buttonPressed==KEYPAD_SELECT || buttonPressed==KEYPAD_UP || buttonPressed==KEYPAD_DOWN));

    switch(buttonPressed)
    {
    case KEYPAD_UP:
      state--;
      if (state=-1) {
        state=3;        
      }
      // сменить режим на экране
      break;
    case KEYPAD_DOWN:
      state++;
      if (state=3) {
        state=0;        
      }
      // сменить режим на экране
      break;
    case KEYPAD_SELECT:
      switch(state)
      {
      case 0:
        // cur_time + cur_task

        break;
      case 1:
        // last_irr

        break;
      case 2:
        // next_task_time, next_task_type

        break;
        //      default:
      }
      //    default:
    }
  }
}








