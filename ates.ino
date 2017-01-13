
typedef enum { NONE, GOT_SYNC, GOT_NODEID, GOT_BYTE } states;
states state = NONE;

byte rep4[10] = { 0xE0, 0, 7, 1, 1, 0, 0, 0, 0x60, 0x69};
byte rep24[30] = { 0xE0, 0, 27, 1, 1, 0x3C, 0x49, 0x4E, 0x49, 0x54, 0x20, 0x43, 0x4F, 0x4D, 0x50, 0x4C, 0x45, 0x54, 0x45, 0x21, 0x3E, 0, 0, 0x20, 0, 0xFF, 0xFF, 0xDF, 0xFF, 0x61 };
byte rtcinfo[13] = { 0xE0, 0 , 10, 1, 1, 99, 2, 12, 1, 12, 0, 0, 0x8A };
byte deviceid[39] = { 0xE0, 0, 0x24, 1, 1, 0x4B, 0x4F, 0x4E, 0x41, 0x4D, 0x49, 0x20, 0x43, 0x4F, 0x2E, 0x2C, 0x4C, 0x54, 0x44, 0x2E, 0x3B, 0x4D, 0x61, 0x73, 0x74, 0x65, 0x72, 0x20, 0x43, 0x61, 0x6C, 0x65, 0x6E, 0x64, 0x61, 0x72, 0x3B, 0, 0x1F };
byte replybuf[30] = { 0xE0, 0 };
byte recvbuf[30] = { 0 };
typedef enum { NONE_ACK, EMPTY_ACK, DIPSW_ACK, REGION_ACK} replymodes;
replymodes replymode = NONE_ACK;

#define rs485_tx HIGH
#define rs485_rx LOW
const int serialctl = 5; 
const int jvs_sense = 23;

byte totalbytes, chksm, cmdb, irecvbuf, tempsec;

/*
void calc_sum()
{
  byte sum = 0;
  for (int i = 1; i < 38; i++)
  {
    sum += deviceid[i];
  }
  Serial.println("");
  Serial.print("sum = "); Serial.println(sum, HEX);
}*/

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);  // to pc
  Serial3.begin(115200); // to rs485
  pinMode(serialctl, OUTPUT);
  pinMode(jvs_sense, INPUT);
  digitalWrite(jvs_sense, LOW);
  digitalWrite(serialctl, rs485_rx);
  state = NONE; replymode = NONE_ACK; tempsec = 0;
  Serial.println("test mode jvs");
}

void rs485_send(const byte *addr, byte len)
{
  digitalWrite(serialctl, rs485_tx);
  //delayMicroseconds(30);
  Serial3.write(addr, len);
  while (!(UCSR3A & (1 << UDRE3)))  // Wait for empty transmit buffer
     UCSR3A |= 1 << TXC3;  // mark transmission not complete
  while (!(UCSR3A & (1 << TXC3)));   // Wait for the transmission to complete
  //delayMicroseconds(30);
  digitalWrite(serialctl, rs485_rx);
}

void reply()
{
  switch(replymode)
  {
    case NONE_ACK: break;
    case EMPTY_ACK: replybuf[2] = 3; replybuf[3] = 1; replybuf[4] = 1; replybuf[5] = 5; rs485_send(replybuf, 6); break;
    case REGION_ACK: replybuf[2] = 4; replybuf[3] = 1; replybuf[4] = 1; replybuf[5] = 5; replybuf[6] = 11; rs485_send(replybuf, 7); break;
    case DIPSW_ACK:            break;
    default: break;
  }
}

void processIncomingByte (const byte c)
{  
  if (c == 0xE0) Serial.println("");
  Serial.print(c, HEX); Serial.print(" ");
  switch(state)
  {
    case NONE: if (c == 0xE0) {state = GOT_SYNC; replymode = NONE_ACK;} break;
    case GOT_SYNC: chksm = c; state = GOT_NODEID; break;
    case GOT_NODEID: totalbytes = c; chksm += c; state = GOT_BYTE; irecvbuf = c; break;
    case GOT_BYTE:
    {
      recvbuf[irecvbuf - totalbytes] = c;
      if (totalbytes == 1)  { if (chksm != c) Serial.println("wrong checksum!"); } else chksm += c;
      if (totalbytes > 0)
      {
        totalbytes--; 
        if (totalbytes == 0) 
        {
          state = NONE;
          switch(recvbuf[0]) //payload
          {          
            case 0xF0:
              //Serial.println("reset"); 
              pinMode(jvs_sense, INPUT); 
              break; 
            case 0xF1:
              //Serial.println("setaddr");  replymode = EMPTY_ACK; 
              if (recvbuf[1] == 1) replymode = EMPTY_ACK; 
              reply();
              pinMode(jvs_sense, OUTPUT);
              break; 
            case 0x2F:
              //Serial.println("resend"); 
              break;
            case 0x10:
              rs485_send(deviceid, 39);
              break;
            case 0x71: //E0 01 05 71 FF FF 01 76
              //game region info
              replymode = REGION_ACK; reply();
              break;
            case 0x70: //E0 ...
              //calendar info for rtc (m48t58y)
              rs485_send(rtcinfo, 13);
              tempsec++; rtcinfo[11] = tempsec; rtcinfo[12]++;
              break;
            //E0 01 05 7C 7F 00 04 05 - expect 4 bytes reply
            //E0 01 05 7C 80 00 18 1A - expect 24 bytes reply
            //E0 01 0D 7D 80 10 08 00 00 20 01 FF FF DF FE 1F - reply empty ack - this cmd update rep24 content
            //next 7c after 7d cmd should returned updated content of rep24
            case 0x7c:
              if (recvbuf[3] == 4) rs485_send(rep4, 10); else if (recvbuf[3] == 0x18) rs485_send(rep24, 30);
              break;
            case 0x7d:
              for (int i = 4; i < 12; i++)
                rep24[17+i] = recvbuf[i];
              //should recalc chksm, but since the changes are negated each other, chksm still same
              //recvbuf[4] - recvbuf[11]
              //update rep24 buffer rep24[21] - rep24[28]
              replymode = EMPTY_ACK; reply();
              break;
            //E0 01 03 7E 40 C2 - tells main-id and sub-id created from seed
            case 0x7e:
              replymode = EMPTY_ACK; reply();
              break;
            default: 
              Serial.print("not handled : "); Serial.println(recvbuf[0], HEX); while(1); 
              break;
          }
        }
      }
      break;
    }
    default: break;
  }
}

void loop() {
  // put your main code here, to run repeatedly: 
  if (Serial3.available())
    processIncomingByte (Serial3.read());
}
