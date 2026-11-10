/*
  This file contains code to emulate an X32 to communicate with the Behringer ExpansionCards
*/

// Keep-alive: the DN32-WSG needs *8S# once a second and answers with *8YY#.
// First Y: status-command acknowledged, second Y: external word clock is used.
bool card1KeepAlive = false;
bool card2KeepAlive = false;
uint32_t lastKeepAlive = 0;
String lastStatus1 = "";
String lastStatus2 = "";
uint32_t lastStatusRx[3] = {0, 0, 0};     // time of last status-reply, index = card number
uint32_t lastNoReplyWarn[3] = {0, 0, 0};

// enable the keep-alive for a card if it identified as DN32-WSG
bool enableKeepAliveIfNeeded(uint8_t card, const String& ans) {
  bool needed = (ans.indexOf("*8DN32-WSG") != -1);
  if (needed) {
    lastStatusRx[card] = millis();
    Serial.println("       DN32-WSG found in slot #" + String(card) + ", keep-alive *8S# enabled");
  } else {
    Serial.println("       Card in slot #" + String(card) + " is no DN32-WSG, no keep-alive");
  }
  return needed;
}

// returns true if msg was a status-reply and has been handled
bool handleStatusReply(uint8_t card, const String& msg, String& lastStatus) {
  if ((msg.length() != 4) || !msg.startsWith("*8") || ((msg.charAt(2) != 'Y') && (msg.charAt(2) != 'N'))) {
    return false;
  }
  lastStatusRx[card] = millis();
  if (msg != lastStatus) { // only report changes
    lastStatus = msg;
    Serial.print("XCard #" + String(card) + " status: " + msg);
    Serial.print(msg.charAt(2) == 'Y' ? F(" | acknowledged") : F(" | NOT acknowledged"));
    Serial.println(msg.charAt(3) == 'Y' ? F(" | external word clock in use") : F(" | external word clock NOT in use"));
  }
  return true;
}

// send *8S# and warn if the card does not answer with *8YY#
void sendKeepAlive(uint8_t card, Print& port) {
  port.print("*8S#");
  uint32_t now = millis();
  if ((now - lastStatusRx[card] > 5000) && (now - lastNoReplyWarn[card] > 5000)) {
    lastNoReplyWarn[card] = now;
    Serial.println("XCard #" + String(card) + ": no status-reply to *8S#");
  }
}

// send the keep-alive-command once a second to all cards that need it
void handleKeepAlive() {
  if (millis() - lastKeepAlive < 1000) {
    return;
  }
  lastKeepAlive = millis();

  if (card1KeepAlive) {
    sendKeepAlive(1, SerialCard1);
  }
  if (card2KeepAlive) {
    sendKeepAlive(2, SerialCard2);
  }
}

void handleXCard1Communication() {
  String msg;
  if (SerialCard1.available() > 0) {
    msg = SerialCard1.readStringUntil('#');
    if (!(card1KeepAlive && handleStatusReply(1, msg, lastStatus1))) {
      Serial.println("XCard #1: " + msg);
    }
  }
}

void handleXCard2Communication() {
  String msg;
  if (SerialCard2.available() > 0) {
    msg = SerialCard2.readStringUntil('#');
    if (!(card2KeepAlive && handleStatusReply(2, msg, lastStatus2))) {
      Serial.println("XCard #2: " + msg);
    }
  }
}

// identify-response of the cards:
//   *8X-UREC:A:12#  *8X-USB:A:3#  *8X-DANTE:A:1#  *8X_MADI:A:1#  (X32 cards)
//   *8DN32-WSG:A:1#  *8DN32-UREC:A:12#                           (DN32 cards)
bool isIdentifyResponse(const String& ans) {
  return (ans.indexOf("*8X") != -1) || (ans.indexOf("*8DN32") != -1);
}

// Some cards (e.g. DN32-WSG) expect the identify-commands of the other slots before their own *8I#.
// The X32 sends *0I#, *1I# and *4I# 20 ms apart. Cards that don't need them ignore them.
void sendSlotProbe(Print& port) {
  Serial.println(F("       Sending *0I#, *1I#, *4I#..."));
  port.print("*0I#");
  delay(20);
  port.print("*1I#");
  delay(20);
  port.print("*4I#");
  delay(20);
}

// All cards need a *8R# 20 ms after the *8I# (identifySent = millis() when *8I# was sent)
void x32FollowUpIdentify(Stream& port, uint32_t identifySent) {
  while (millis() - identifySent < 20) {
    delay(1);
  }
  Serial.println(F("       Sending *8R#..."));
  port.print("*8R#");
  String reply = port.readStringUntil('#');
  Serial.println("    Response " + reply);
}

void x32InitCard1() {
  delay(1000); // X32 would take some time to boot. So we have to be patient and wait here some time to let X-Card initialize
  Serial.println(F("   waiting... 3s"));
  delay(1000); // X32 would take some time to boot. So we have to be patient and wait here some time to let X-Card initialize
  Serial.println(F("   waiting... 2s"));
  delay(1000); // X32 would take some time to boot. So we have to be patient and wait here some time to let X-Card initialize
  Serial.println(F("   waiting... 1s"));
  delay(1000); // X32 would take some time to boot. So we have to be patient and wait here some time to let X-Card initialize
  Serial.println(F("   wait for CardReady-Signal..."));
  
  // wait until card is ready (that's when CardRead-Signal is high)
  while(!digitalRead(CARD1_READY_PIN)) {
    delay(10);
  }
  delay(100);

  // now initialize the card #1
  Serial1Flush(); // flush serial for card #1
  String ans;
  Serial.println(F("   Initializing Card #1..."));
  sendSlotProbe(SerialCard1);
  Serial.println(F("       Sending *8I#..."));
  SerialCard1.print("*8I#"); // send identify-command
  uint32_t identifySent = millis();
  ans = SerialCard1.readStringUntil('#'); // ans should be *8X-UREC:A:12# or *8DN32-WSG:A:1#
  while (!isIdentifyResponse(ans)) {
    Serial.println("       Ignoring response " + ans);
    ans = SerialCard1.readStringUntil('#');
  }
  Serial.println("    Found " + ans);
  card1KeepAlive = enableKeepAliveIfNeeded(1, ans);
  bool card1IsXUsb = (ans.indexOf("*8X-USB") != -1);
  x32FollowUpIdentify(SerialCard1, identifySent);

  delay(250);
/*
  Serial.println(F("       Sending *8R#..."));
  SerialCard1.print("*8R#"); // send second identify-command
  ans = SerialCard1.readStringUntil('#'); // ans should be *8X-UREC:A:12#
  while (ans.indexOf("*8X") == -1) {
    Serial.println("       Ignoring response " + ans);
    ans = SerialCard1.readStringUntil('#'); // ans should be *8X-UREC:A:12#
  }
  Serial.println("    Found " + ans);

  delay(250);
*/

  // now send a bunch of initialization commands - don't know for what - just doing what the X32 is doing
  Serial.println(F("       Sending long Init-Command..."));
  for (uint8_t i=0; i<8; i++) {
    SerialCard1.print("*1G" + String(i) + "0000#");
    if (i == 0) {
      SerialCard1.print("*1G" + String(i) + "0001#");
    } else {
      SerialCard1.print("*1G" + String(i) + "0000#");
    }
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard1.print("*0G" + String(i) + "0000#");
    SerialCard1.print("*0G" + String(i) + "0000#");
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard1.print("*3G" + String(i) + "0000#");
    SerialCard1.print("*3G" + String(i) + "0000#");
    if (i == 0) {
       delay(18);
    }
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard1.print("*2G" + String(i) + "0000#");
    SerialCard1.print("*2G" + String(i) + "0000#");
  }
  SerialCard1.print("*0R:W018B#");
  SerialCard1.print("*1R:W018B#");
  SerialCard1.print("*4R:W0281C0:W0800:W0280#");
  SerialCard1.print("*8S#");
  delay(120);
  lastKeepAlive = millis();
  SerialCard1.print("*8S#");

  // set Card to USB-Mode
  if (card1IsXUsb) {
    Serial.println(F("       X-USB found, sending *8C80#..."));
    SerialCard1.print("*8C80#");
  } else {
    //Serial.println(F("       Set Card-Mode to USB via *8C80U#..."));
    //SerialCard1.print("*8C80U#"); // 32 channels from USB
  }
  Serial.println(F("   Done."));
}


void x32InitCard2() {
  // wait up to 10s until card is ready (that's when CardReady-Signal is high)
  Serial.println(F("   wait for CardReady-Signal of Card #2..."));
  uint16_t readyTimeout = 200;
  while(!digitalRead(CARD2_READY_PIN) && (readyTimeout > 0)) {
    delay(10);
    readyTimeout--;
  }
  if (!digitalRead(CARD2_READY_PIN)) {
    Serial.println(F("   Card #2 is not ready - sorry :-("));
    return;
  }
  delay(100);

  // now initialize the card #2
  String ans;
  Serial.println(F("   Initializing Card #2..."));

  // the probe-commands for the other slots have to be sent first
  Serial2Flush(); // flush serial for card #2
  sendSlotProbe(SerialCard2);

  Serial.println(F("       Sending *8I#..."));

  // if we can not communicate with card #2, exit
  SerialCard2.print("*8I#"); // send identify-command
  uint32_t identifySent = millis();

  if (!waitForCard2Response(2)) {
    Serial.println(F("   Could not find Card #2 - sorry :-("));
    return;
  }

  ans = SerialCard2.readStringUntil('#'); // ans should be *8X-UREC:A:12# or *8DN32-WSG:A:1#
  while (!isIdentifyResponse(ans)) {
    Serial.println("       Ignoring response " + ans);
    ans = SerialCard2.readStringUntil('#');
  }
  Serial.println("    Found " + ans);
  card2KeepAlive = enableKeepAliveIfNeeded(2, ans);
  bool card2IsXUsb = (ans.indexOf("*8X-USB") != -1);
  x32FollowUpIdentify(SerialCard2, identifySent);

  delay(250);
/*
  Serial.println(F("       Sending *8R#..."));
  SerialCard2.print("*8R#"); // send second identify-command
  ans = SerialCard2.readStringUntil('#'); // ans should be *8X-UREC:A:12#
  while (ans.indexOf("*8X") == -1) {
    Serial.println("       Ignoring response " + ans);
    ans = SerialCard2.readStringUntil('#'); // ans should be *8X-UREC:A:12#
  }
  Serial.println("    Found " + ans);

  delay(250);
*/

  // now send a bunch of initialization commands - don't know for what - just doing what the X32 is doing
  Serial.println(F("       Sending long Init-Command..."));
  for (uint8_t i=0; i<8; i++) {
    SerialCard2.print("*1G" + String(i) + "0000#");
    if (i == 0) {
      SerialCard2.print("*1G" + String(i) + "0001#");
    } else {
      SerialCard2.print("*1G" + String(i) + "0000#");
    }
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard2.print("*0G" + String(i) + "0000#");
    SerialCard2.print("*0G" + String(i) + "0000#");
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard2.print("*3G" + String(i) + "0000#");
    SerialCard2.print("*3G" + String(i) + "0000#");
  }
  for (uint8_t i=0; i<8; i++) {
    SerialCard2.print("*2G" + String(i) + "0000#");
    SerialCard2.print("*2G" + String(i) + "0000#");
  }
  SerialCard2.print("*0R:W018B#");
  SerialCard2.print("*1R:W018B#");
  SerialCard2.print("*4R:W0281C0:W0800:W0280#");
  SerialCard2.print("*8S#");
  delay(120);
  SerialCard1.print("*8S#");

  //delay(card2IsXUsb ? 2000 : 1000);

  // set Card to USB-Mode
  if (card2IsXUsb) {
    Serial.println(F("       X-USB found, sending *8C80#..."));
    SerialCard2.print("*8C80#");
  } else {
    //Serial.println(F("       Set Card-Mode to USB via *8C80U#..."));
    //SerialCard2.print("*8C80U#"); // 32 channels from USB
  }
  Serial.println(F("   Done."));
}

bool waitForCard2Response(uint8_t maxSeconds) {
  uint8_t cardTimeoutCounter = 0;
  while ((SerialCard2.available() < 4) && (cardTimeoutCounter < maxSeconds)) { // we expect either *8BE# or *8X-UREC:A:12#
    Serial.println(F("       Waiting for response..."));
    cardTimeoutCounter += 1;
    delay(1000);
  }
  Serial.println("       We received " + String(SerialCard2.available()) + "bytes from card...");
  return (SerialCard2.available() >= 4);
}
