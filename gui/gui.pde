// see www.processing.org
//start the processing sketch first, then run the program on micro.

import processing.serial.*;

Serial myPort;    // The serial port:
String inString;  // Input string from serial port: COM3

String string_A;  
String string_B;  
int val_A;
int val_B;

int lf = 10;      // ASCII linefeed

void setup() {
  size(350, 255);

  // List all the available serial ports:
  println(Serial.list());

  // Open whatever port is the one you're using. For us it's COM3, which is [2]
  myPort = new Serial(this, Serial.list()[2], 9600); 
  myPort.bufferUntil(lf);
}

void draw() {
  background(110, 150, 200);
  
//  text("Channel A: " + string_A, 10,50);
//  text("Channel B: " + string_B, 10,50);
  
  rect(50, 190 - val_A, 100, 25);
  rect(200, 225 - val_B, 100, 25);
  
  line(50, 180, 150, 180);
  line(200, 180, 300, 180);    
  
}

void serialEvent(Serial p) {
  inString = (myPort.readString());

  string_A = inString.split(",") [0].trim();
  string_B = inString.split(",") [1].trim();

  val_A = Integer.parseInt(string_A);
  val_B = Integer.parseInt(string_B);
  
  System.out.print(string_A);
  System.out.print(", ");
}







