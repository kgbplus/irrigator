#include <Wire.h>
#include <EEPROM.h>
#include "RTClib.h"
#include <SPI.h>
#include <Mirf.h>
#include <nRF24L01.h>
#include <MirfHardwareSpiDriver.h>

RTC_DS1307 RTC;

const int HUMIDY_MIN = 200;
const int HUMIDY_MAX = 700;

const int8_t t_1ST_IRR_TIME = 6;
const int8_t t_2ND_IRR_TIME = 21;

const int WATER_PUMP1_TIME = 30000; // 30 sec
const int WATER_PUMP2_TIME = 30000; // 30 sec

#define HUMIDY_SENSOR        A0
#define HUMIDY_SENSOR_POWER  2
#define BAT_SENSOR           A1
#define CLOCK_SDA            A4
#define CLOCK_SDL            A5
#define BAT_TEST             A1
#define RELAY1               3
#define RELAY2               4
#define RADIO_CE             7
#define RADIO_CSN            10
#define IRR_BUTTON         5
#define IRR_LED            6

// Scheduler struct. task_type = 1 (irrigation), 2 (radio packet send)
struct task{
  DateTime time;
  int task_type;
} 
schedule[10];

struct radio_send_packet{
  unsigned int humidy;
  long unsigned int cur_time;
  long unsigned int last_irr;
  int cur_task;      // 0 if waiting
  long unsigned int next_task_time;
  int next_task_type;
  unsigned int bat_level;
}
RSP;

struct radio_receive_packet{
  long unsigned int time;  // 0 if not set
  int task;  // 0 if none
}
RRP;  

const long radio_send_packet_sz = sizeof(radio_send_packet);
const long radio_receive_packet_sz = sizeof(radio_receive_packet);

int tasks = 0;
int cur_task = 0;
DateTime last_irr = 0; //DateTime(0,0,0,0,0,0);
static unsigned long wait1 = 0;
static unsigned long wait2 = 0;
static int battery_level, irr_button_state = 0;

// ********************************************
// Блок исполнительных процедур (включение устройств)
// ********************************************
bool irrigate1(void)
{
  Serial.println("Water pump 1 ON");
  digitalWrite(RELAY1, HIGH);
  wait1 = millis() + WATER_PUMP1_TIME;
  last_irr = RTC.now();
  cur_task = 1;
}

bool irrigate2(void)
{
  Serial.println("Water pump 2 ON");
  digitalWrite(RELAY2, HIGH);
  wait2 = millis() + WATER_PUMP2_TIME;
  cur_task = 2;
}

// ********************************************
// Процедуры печати диагностических сообщений
// ********************************************
void print_dt(DateTime dt)
{
  Serial.print(dt.day(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.year(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  Serial.print(dt.second(), DEC);
}

void print_sched_line(int i)
{
  print_dt(schedule[i].time);
  Serial.print(" : type = ");
  Serial.println(schedule[i].task_type);
}

void print_sched(void)
{
  for (int i=0; i<tasks; i++) {
    print_sched_line(i);
  }
  Serial.println("--------------------------------");
}

// ********************************************
// Чтение датчиков, проверка выполнения условий
// ********************************************
unsigned int readHumidy(void)
{
  unsigned int val=0;

  // Switch sensor power on and wait for warm up
  digitalWrite(HUMIDY_SENSOR_POWER, HIGH);
  delay(200);

  for(int i = 0; i<3; i++) val+=analogRead(HUMIDY_SENSOR);

  val/=3;

  digitalWrite(HUMIDY_SENSOR_POWER, LOW); // Sensor power off

  Serial.print("Humidy sensor = ");
  Serial.println(val);
  return val;
}

bool humidy_low(void)
{
  if (readHumidy() > HUMIDY_MAX) return true;
  else return false;
}

unsigned int read_battery(void)
{
  unsigned int val=0;

  for(int i = 0; i<3; i++) val+=analogRead(BAT_SENSOR);

  val/=3;

  Serial.print("Battery level = ");
  Serial.println(val);
  return val;
}

//void processSyncMessage() {
//  // if time sync available from serial port, update time and return true
//  while(Serial.available() >=  TIME_MSG_LEN ){  // time message consists of header & 10 ASCII digits
//    char c = Serial.read() ;
//    Serial.print(c);  
//    if( c == TIME_HEADER ) {      
//      time_t pctime = 0;
//      for(int i=0; i < TIME_MSG_LEN -1; i++){  
//        c = Serial.read();          
//        if( c >= '0' && c <= '9'){  
//          pctime = (10 * pctime) + (c - '0') ; // convert digits to a number    
//        }
//      }  
//      setTime(pctime);   // Sync Arduino clock to the time received on the serial port
//    }  
//  }
//}

// ********************************************
// Процедуры работы с планировщиком
// ********************************************
bool execute_task(int task)
{
  bool res;
  switch (schedule[task].task_type)
  {
  case 1:
    if(humidy_low()) {                 // Test humidy_low()
      res = irrigate1();
    }
    return true;
    break;
  case 2:
    res = irrigate2();
    return true;
    break;
  default:
    return false;
  }
}

void save_schedule(void) {
  int address = 0;
  EEPROM.write(address++,0xfa); // Signature
  EEPROM.write(address++,0xea);
  EEPROM.write(address++,tasks);
  for (int i=0; i<tasks; i++) {
    EEPROM.write(address++, (unsigned char)(schedule[i].time.year()-2000));
    EEPROM.write(address++, schedule[i].time.month());
    EEPROM.write(address++, schedule[i].time.day());
    EEPROM.write(address++, schedule[i].time.hour());
    EEPROM.write(address++, schedule[i].time.minute());
    EEPROM.write(address++, schedule[i].time.second());
    EEPROM.write(address++, schedule[i].task_type);
  }
  Serial.println("Schedule saved to EEPROM");
  Serial.println("--------------------------------");
}

void erase_schedule(void) {
  int address = 0;
  EEPROM.write(address++,0x00); // Signature
  EEPROM.write(address++,0x00);
}

void read_schedule(void) {
  int address = 0;
  if (EEPROM.read(address++) == 0xfa && EEPROM.read(address++) == 0xea) {
    tasks = EEPROM.read(address++);
    for (int i=0; i<tasks; i++) {
      schedule[i].time = DateTime(2000 + EEPROM.read(address),EEPROM.read(address+1),EEPROM.read(address+2),EEPROM.read(address+3),EEPROM.read(address+4),EEPROM.read(address+5));
      address+=6;
      schedule[i].task_type = EEPROM.read(address++);
    }
    Serial.println("EEPROM tasks loaded. Full schedule:");
    print_sched();
  }
}

void delete_task(int task)
{
  for(int i=task; i<tasks; i++) {
    schedule[i].time = schedule[i+1].time;
    schedule[i].task_type = schedule[i+1].task_type;
  }
  tasks--;
  // save to eeprom
  save_schedule();
  Serial.println("Task deleted. Current schedule:");
  print_sched();
}

void reschedule(DateTime dt)
{
  DateTime last_task;
  TimeSpan ts;
  int32_t i_ts;
  int days = 1;

  if (tasks<10) { // Schedule not full
    if (tasks>0) { // Schedule not empty
      if (dt.year() == schedule[tasks-1].time.year() && dt.month() == schedule[tasks-1].time.month() && dt.day() == schedule[tasks-1].time.day()) { // Today task if needed
        if (dt.hour() < 21 && schedule[tasks-1].time.hour() == t_1ST_IRR_TIME && tasks < 10) {
          schedule[tasks].time = DateTime(dt.year(), dt.month(), dt.day(), t_2ND_IRR_TIME, 0, 0);
          schedule[tasks].task_type = 1;
          tasks++;
        }
      }

      if (tasks > 0) {
        ts = schedule[tasks-1].time - dt;  // Timespan between last task and dt
        i_ts = ts.totalseconds();

        if (i_ts > 0)
          last_task = schedule[tasks-1].time;
        else
          last_task = dt;    // Last task outdated
      } 
      else
        last_task = dt;

      for (int i=tasks; i<10; i++) {
        schedule[i].time = DateTime(last_task.year(), last_task.month(), last_task.day(), 0, 0, 0) + TimeSpan(days, t_1ST_IRR_TIME, 0, 0); // Next day tasks
        schedule[i].task_type = 1;
        tasks++;
        if (tasks<10) {
          i++;
          schedule[i].time = DateTime(last_task.year(), last_task.month(), last_task.day(), 0, 0, 0) + TimeSpan(days, t_2ND_IRR_TIME, 0, 0);
          schedule[i].task_type = 1;
          tasks++;
          days++;
        }      
      }
      // save to eeprom
      save_schedule();
      Serial.println("Schedule updated (reschedule)");
      print_sched();
    } 
    else 
    {   // Schedule empty
      if (dt.hour() < 6) {
        schedule[tasks].time = DateTime(dt.year(), dt.month(), dt.day(), t_1ST_IRR_TIME, 0, 0);
        schedule[tasks].task_type = 1;
        tasks++;
      }
      if (dt.hour() < 21) {
        schedule[tasks].time = DateTime(dt.year(), dt.month(), dt.day(), t_2ND_IRR_TIME, 0, 0);
        schedule[tasks].task_type = 1;
        tasks++;
      }
      for (int i=tasks; i<10; i++) {
        schedule[tasks].time = DateTime(dt.year(), dt.month(), dt.day(), 0, 0, 0) + TimeSpan(days, t_1ST_IRR_TIME, 0, 0);
        schedule[tasks].task_type = 1;
        tasks++;
        if (tasks<10) {
          i++;
          schedule[tasks].time = DateTime(dt.year(), dt.month(), dt.day(), 0, 0, 0) + TimeSpan(days, t_2ND_IRR_TIME, 0, 0);
          schedule[tasks].task_type = 1;
          tasks++;
          days++;
        }
      }
    } 
  }
  else
  { // Schedule full
    print_dt(RTC.now());
    Serial.println(": Reschedule started, but did nothing");
  }
}   

void check_schedule(DateTime dt)
{
  TimeSpan ts;
  int32_t i_ts;
  for(int i=0; i<tasks; i++) {
    ts = schedule[i].time - dt;
    i_ts = ts.totalseconds();
    if(i_ts  > -1200 && i_ts <= 0) { // Task executed
      Serial.println("Task executed");
      print_sched_line(i);
      execute_task(i);
      delete_task(i--);
    }
    if(i_ts < -1200) { // Task outdated
      Serial.println("Task outdated and deleted");
      print_sched_line(i);
      delete_task(i--);
    }
  }
  reschedule(dt);
}

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
   * Configure receiving address.
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

  Mirf.send((byte *)packet);

  while(Mirf.isSending()){
  }
  Serial.println("Finished sending");

  Mirf.payload = radio_receive_packet_sz;
  Mirf.config();
}

// ********************************************
// Основная программа
// ********************************************
void setup(void)
{
  //Init pins
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  pinMode(IRR_BUTTON, INPUT);
  digitalWrite(IRR_BUTTON, HIGH);
  pinMode(IRR_LED, OUTPUT);
  pinMode(HUMIDY_SENSOR_POWER, OUTPUT);
  digitalWrite(HUMIDY_SENSOR_POWER, LOW);
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, HIGH);

  // Init communications
  Serial.begin(57600);
  Serial.println();
  Serial.println();
  Serial.println();
  Serial.println("Irrigator started");  

  Serial.println("Humidy sensor check...");
  if (readHumidy() == 0) {  // Check if humidy sensor connected
    Serial.println("Humidy sensor fault!");
  }

  // Init RTC
  Serial.println("RTC initialization...");
  battery_level = read_battery();
  Wire.begin();
  RTC.begin();
  if (!RTC.isrunning()) {       // Init RTC if not running
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
    RTC.adjust(DateTime(__DATE__, __TIME__));
  }
  Serial.print("System time: ");
  print_dt(RTC.now());
  Serial.println();

  // Relay module test
  Serial.println("Relay module test...");
  digitalWrite(RELAY1,HIGH);   
  digitalWrite(RELAY2,LOW);
  delay(1000);
  digitalWrite(RELAY2,HIGH);
  digitalWrite(RELAY1,LOW);

  // Init Mirf
  setup_Mirf();


  // Init scheduler. Календарь нужен на случай, когда будем записывать задания в eeprom
  // В этом случае, инициализация сначала проверяет нет ли не выполненных заданий в памяти
  // и если нет, то формирует таблицу
  //erase_schedule();
  read_schedule();
  if (tasks == 0) { //EEPROM couldn't be read
    Serial.println("EEPROM coludn't be read!");
    DateTime now = RTC.now();
    Serial.print("Schedule init on :");
    print_dt(now);
    Serial.println(); 
    reschedule(now);
    Serial.println("Schedule initialized");
    print_sched();
  }
}

void loop(void)
{
  check_schedule(RTC.now());

  if(wait1 != 0 && wait1 < millis()) { // Check if pump is on
    Serial.println("Water pump 1 OFF");
    digitalWrite(RELAY1, LOW);
    digitalWrite(IRR_LED, LOW); 
    wait1 = 0;
    cur_task = 0;
  };

  if(wait2 != 0 && wait2 < millis()) { // Check if pump is on
    Serial.println("Water pump 2 OFF");
    digitalWrite(RELAY2, LOW);
    wait2 = 0;
    cur_task = 0;
  };

  // read the state of the pushbutton value:
  irr_button_state = digitalRead(IRR_BUTTON);

  // принудительный запуск полива
  if (irr_button_state == LOW && wait1 == 0) {        
    digitalWrite(IRR_LED, HIGH);
    irrigate1();  
  } 

  // radio exchange
  if (radio_receive(&RRP)){

    //    обработать принятый пакет
    if (RRP.time > 0) {
      RTC.adjust(RRP.time);

      Serial.println("Time adjusted from radio");
      Serial.print("System time: ");
      print_dt(RTC.now());
      Serial.println();
    }

    if (RRP.task > 0) {
      execute_task(RRP.task);
      Serial.print("Task executed from radio: "); 
      Serial.println(RRP.task);
    }
    
    //    создать ответ
    RSP.humidy = readHumidy();
    RSP.cur_time = RTC.now().unixtime();
    RSP.last_irr = last_irr.unixtime();
    RSP.cur_task = cur_task;
    if(tasks>0) {
      RSP.next_task_time = schedule[0].time.unixtime();
      RSP.next_task_type = schedule[0].task_type;
    } else {
      RSP.next_task_time = 0;//DateTime(0,0,0,0,0,0);
      RSP.next_task_type = 0;
    }
    RSP.bat_level = read_battery(); // Not implemented
    
    radio_send(&RSP);
  }

  delay(100);
}








