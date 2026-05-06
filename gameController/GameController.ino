#define FPS 30

#include <WIFI.h>
#include <WiFiUDP.h>

#include <TFT_eSPI.h>

#define _clamp(val,minVal,maxVal) _max(minVal,_min(val,maxVal))

// These display definitions are taken from https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/blob/main/Examples/Basics/1-HelloWorld/1-HelloWorld.ino
// Display definitions
#define FONT 2
#define FONT_SIZE_PX 16

#define LARGE_FONT 6
#define LARGE_FONT_SIZE_PX 48


// Network definitions
#include <network_protocol.hpp>

const char* ssid = "ESP32_AP";
const char* password = "password123";

WiFiUDP Udp;
unsigned int localPort = 1234;
uint8_t packetBuffer[PACKET_MAXLEN];

TFT_eSPI tft = TFT_eSPI();

// Gameplay definitions
#define STARTING_BALL_Y_VEL 3
#define DEADZONE_ANGLE 2.5f
#define CONTROLLER_SENSITIVITY 1.2f

bool gameStart = false;

struct Paddle {
  int oldX, oldY;
  int x, y;
  int vy;
  int ay;
  int w, h;
  int color;
  bool isReady;
  bool lastButtonState;
};

struct Ball {
  int oldX, oldY;
  int x, y;
  int vx, vy;
  int r;
  int color;
};

Paddle paddles[2];
Ball* ball;

int score1 = 0, score2 = 0;

int aiTargetY = 0;
int aiErrorOffset = 0;
long lastErrorUpdate = 0;

long startTimer;
int bounceCounter = 0;
int maxBallXVel = 6;
const float ballPaddleMaxBounceAngle = 80 * (PI/180); // convert to rad

int connectedPlayers = 0;


void setup() {
  Serial.begin(115200);
  Serial.println();


  // Start the tft display and set it to black
  tft.init();
  tft.setRotation(1); //This is the display in landscape
  
  // Clear the screen before writing to it
  tft.fillScreen(TFT_BLACK);

  // set Access Point Mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  // pinMode(16, OUTPUT);

  // start UDP
  Udp.begin(localPort);

  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("UDP listening on:", TFT_WIDTH/2, TFT_WIDTH/2-FONT_SIZE_PX/2, FONT);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString(WiFi.softAPIP().toString() + ":" + localPort, TFT_WIDTH/2, TFT_WIDTH/2+FONT_SIZE_PX/2, FONT);

  // paddles[0] = (Paddle*)malloc(sizeof(Paddle));
  // paddles[1] = (Paddle*)malloc(sizeof(Paddle));
  ball = (Ball*)malloc(sizeof(Ball));
}

void initGameState();
void updateAI();
void reflectBallOffPaddle(Paddle p);

void loop() {
  delay(1.0/FPS*1000);

  // pre game
  if(!gameStart) {
    
    PacketResult r = receivePacket();
    char s[50];
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    sprintf(s, "Received Type %02x", r.type);
    if(r.type != PACKET_NONE) tft.drawString(s, TFT_WIDTH/2, TFT_WIDTH/2+FONT_SIZE_PX*3, FONT);
    switch(r.type) {
      case PACKET_INTRO_TYPE:
        S2C_AssignPacket p;
        p.type = PACKET_ASSIGN_TYPE;
        p.playerId = connectedPlayers++;
        sendPacketTo(Udp.remoteIP().toString().c_str(), &p, sizeof(S2C_AssignPacket));
        sprintf(s, "Player %d", p.playerId+1);
        tft.drawString(s, TFT_HEIGHT/4*(p.playerId == 0 ? 1 : 2), TFT_WIDTH/2+FONT_SIZE_PX*4, FONT);
        tft.drawString("connected!", TFT_HEIGHT/4*(p.playerId == 0 ? 1 : 2), TFT_WIDTH/2+FONT_SIZE_PX*5, FONT);
        break;
      case PACKET_INPUT_TYPE:
        if(r.data.input.playerId >= connectedPlayers) { // ignore, out of bounds player
          break;
        }
        sprintf(s, "Is P%d pressed? %s", r.data.input.playerId, r.data.input.buttonPressed ? "YES" : "NO");
        Serial.println(s);
        if(r.data.input.buttonPressed && !paddles[r.data.input.playerId].lastButtonState) {
          paddles[r.data.input.playerId].isReady = !paddles[r.data.input.playerId].isReady;
          
          sprintf(s, "Player %d", r.data.input.playerId+1);
          tft.setTextColor(paddles[r.data.input.playerId].isReady ? TFT_CYAN : TFT_GREEN, TFT_BLACK);
          tft.drawString(s, TFT_HEIGHT/4*(r.data.input.playerId == 0 ? 1 : 2), TFT_WIDTH/2+FONT_SIZE_PX*4, FONT);
          tft.drawString("connected!", TFT_HEIGHT/4*(r.data.input.playerId == 0 ? 1 : 2), TFT_WIDTH/2+FONT_SIZE_PX*5, FONT);
        }
        paddles[r.data.input.playerId].lastButtonState = r.data.input.buttonPressed;
        break;
    }

    if((connectedPlayers == 1 && paddles[0].isReady) ||
       (connectedPlayers == 2 && paddles[0].isReady && paddles[1].isReady)) {
      gameStart = true;
      tft.fillScreen(TFT_BLACK);

      // init variables
      initGameState();
    }
    return;
  }

  // actual game
  PacketResult r = receivePacket();
  switch(r.type) {
    case PACKET_INPUT_TYPE:
      if(r.data.input.playerId >= connectedPlayers) { // ignore, out of bounds player
        break;
      }
      char s[50];
      
      float currentTilt = r.data.input.tilt / 100.0f;
      
      float normalizedTilt = currentTilt - 19.0f; // correct so that on a flat table it reads zero, 18.0f value gotten through playtesting
      paddles[r.data.input.playerId].oldY = paddles[r.data.input.playerId].y;
      paddles[r.data.input.playerId].y = map(normalizedTilt, -45, 65, 0, TFT_HEIGHT); // map values determined experimentally via playtesting
      
      // sprintf(s, "Received Tilt %.2f for P%d", normalizedTilt, r.data.input.playerId);
      // tft.drawString(s, TFT_WIDTH/2, TFT_WIDTH/2-50, FONT);
      break;
  }

  
  if(startTimer != 0) {
    if(startTimer > millis()) {
      char s[50];

      sprintf(s, "%ld", (startTimer-millis())/1000l);
      tft.drawString(s, TFT_HEIGHT/2, TFT_WIDTH/2-LARGE_FONT_SIZE_PX/2, LARGE_FONT);
    }
    if(startTimer <= millis()) {
      tft.fillRect(TFT_HEIGHT/2, TFT_WIDTH/2-LARGE_FONT_SIZE_PX/2, LARGE_FONT_SIZE_PX, LARGE_FONT_SIZE_PX, TFT_BLACK);
      startTimer = 0;
    }
  }
  
  if(startTimer == 0) {
    // update logic

    // updating x wastes cpu time since it doesnt change
    // updating Y is now done when receiving input packet for the player paddle
    // paddles[0].oldX = paddles[0].x;
    // paddles[0].oldY = paddles[0].y;

    // paddles[1].oldX = paddles[1].x;
    if(connectedPlayers == 1) paddles[1].oldY = paddles[1].y;

    ball->oldX = ball->x;
    ball->oldY = ball->y;

    // update positions
    paddles[0].y += paddles[0].vy;

    if(connectedPlayers == 1) updateAI();
    paddles[1].y += paddles[1].vy;

    ball->x += ball->vx;
    ball->y += ball->vy;

    if(bounceCounter >= 10) {
      ball->vx += ball->vx > 0 ? 1 : -1;
      ball->vy += ball->vy > 0 ? 1 : -1;
      maxBallXVel++;
      bounceCounter = 0;
    }
  }

  // prevent oob paddle movment
  paddles[0].y = _max(0,_min(paddles[0].y,TFT_WIDTH-paddles[0].h));
  paddles[1].y = _max(0,_min(paddles[1].y,TFT_WIDTH-paddles[1].h));

  // check collisions
  if(ball->y <= 0+ball->r || ball->y >= TFT_WIDTH-ball->r) {
    ball->vy *= -1;
    ball->y = _max(0+ball->r, _min(ball->y,TFT_WIDTH-ball->r));
  }
  
  // check paddle collisions
  if((ball->x <= paddles[0].x+paddles[0].w+ball->r && ball->y >= paddles[0].y && ball->y <= paddles[0].y+paddles[0].h) ||
     (ball->x >= paddles[1].x-ball->r && ball->y >= paddles[1].y && ball->y <= paddles[1].y+paddles[1].h)) {
    bounceCounter++;
  }

  // set ball to paddle's edge upon collision and calculate reflection
  if(ball->x <= paddles[0].x+paddles[0].w+ball->r && ball->y >= paddles[0].y && ball->y <= paddles[0].y+paddles[0].h) {
    ball->x = paddles[0].x+paddles[0].w+ball->r;
    
    reflectBallOffPaddle(paddles[0]);
  }

  if(ball->x >= paddles[1].x-ball->r && ball->y >= paddles[1].y && ball->y <= paddles[1].y+paddles[1].h) {
    ball->x = paddles[1].x-ball->r;
    
    reflectBallOffPaddle(paddles[1]);
  }

  // check scoring (screen X border) collisions
  if(ball->x <= -ball->r) {
    score2++;
    initGameState();
  }else if(ball->x >= TFT_HEIGHT+ball->r) {
    score1++;
    initGameState();
  }

  // // draw
  tft.fillRect(paddles[0].oldX, paddles[0].oldY, paddles[0].w, paddles[0].h, TFT_BLACK);
  tft.fillRect(paddles[0].x, paddles[0].y, paddles[0].w, paddles[0].h, paddles[0].color);
  tft.fillRect(paddles[1].oldX, paddles[1].oldY, paddles[1].w, paddles[1].h, TFT_BLACK);
  tft.fillRect(paddles[1].x, paddles[1].y, paddles[1].w, paddles[1].h, paddles[1].color);


  if(startTimer == 0) {
    tft.fillCircle(ball->oldX, ball->oldY, ball->r, TFT_BLACK);
    tft.fillCircle(ball->x, ball->y, ball->r, ball->color);
  }

  // draw score
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char s[50];

  sprintf(s, "%d", score1);
  tft.drawString(s, TFT_HEIGHT/2-20, 10, FONT);
  
  sprintf(s, "%d", score2);
  tft.drawString(s, TFT_HEIGHT/2+20, 10, FONT);


  // debug information
  // sprintf(s, "%.2f", (ball->y-(paddles[0].y+paddles[0].h/2)) / (float)(paddles[0].h/2));
  // tft.drawString(s, 70, 30, FONT);

  // sprintf(s, "%.2f", (ball->y-(paddles[1].y+paddles[1].h/2)) / (float)(paddles[1].h/2));
  // tft.drawString(s, TFT_HEIGHT-70, 30, FONT);

  // sprintf(s, "%d", ball->vy);
  // tft.drawString(s, TFT_HEIGHT/2, TFT_WIDTH-30, FONT);


  
  // tft.drawLine(0, paddles[1].oldY, TFT_WIDTH, paddles[1].oldY, TFT_BLACK);
  // tft.drawLine(0, paddles[1].y, TFT_WIDTH, paddles[1].y, TFT_RED);
  
  // tft.drawLine(0, paddles[1].oldY+5, TFT_HEIGHT, paddles[1].oldY+5, TFT_BLACK);
  // tft.drawLine(0, paddles[1].y+5, TFT_HEIGHT, paddles[1].y+5, TFT_YELLOW);
  
  // tft.drawLine(paddles[1].x-ball->r, 0, paddles[1].x-ball->r, TFT_WIDTH, TFT_RED);
  // tft.drawLine(paddles[1].x-ball->r+5, 0, paddles[1].x-ball->r+5, TFT_WIDTH, TFT_YELLOW);


  // tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // char s[50];
  // sprintf(s, "(%.2f, %.2f, %.2f)", ax, ay, az);
  // tft.fillRect(5, 10, 150, 8, TFT_BLACK);
  // tft.drawString(s, 5, 10, 1); // Left Aligned
  
  // // tft.fillRect(old_px, 50, 40, 40, TFT_BLACK);
  // // tft.fillRect(px, 50, 40, 40, TFT_WHITE);
}

void initGameState() {
  paddles[0].w = 20;
  paddles[0].h = 50;
  paddles[0].x = 10;
  paddles[0].y = TFT_WIDTH/2-paddles[0].h/2;
  paddles[0].color = TFT_GREEN;
  paddles[0].vy = 0;
  paddles[0].oldX = paddles[0].x;
  paddles[0].oldY = paddles[0].y;

  paddles[1].w = 20;
  paddles[1].h = 50;
  paddles[1].x = TFT_HEIGHT-10-paddles[1].w;
  paddles[1].y = TFT_WIDTH/2-paddles[1].h/2;
  paddles[1].color = TFT_CYAN;
  paddles[1].vy = 0;
  paddles[1].oldX = paddles[1].x;
  paddles[1].oldY = paddles[1].y;

  ball->x = TFT_HEIGHT/2;
  ball->y = TFT_WIDTH/2;
  ball->r = 5;
  ball->vx = STARTING_BALL_Y_VEL * (random(0,100) > 50 ? 1 : -1);
  ball->vy = STARTING_BALL_Y_VEL * ((random(0,100)-50) / 50.0f);
  if(ball->vy == 0) {
    ball->vy = STARTING_BALL_Y_VEL;
  }
  ball->color = TFT_WHITE;


  startTimer = millis()+4000; // 3 sec start delay, 1 more to account for time taken until restart
  
  bounceCounter = 0;
  maxBallXVel = STARTING_BALL_Y_VEL;

  tft.fillScreen(TFT_BLACK);
}


void updateAI() {
  if (millis() - lastErrorUpdate > 500) {
    aiErrorOffset = random(-paddles[1].h / 2, paddles[1].h / 2);
    lastErrorUpdate = millis();
  }


  if (ball->vx > 0) {
    aiTargetY = ball->y - (paddles[1].h / 2) + aiErrorOffset;
  } else {
    aiTargetY = (TFT_WIDTH / 2) - (paddles[1].h / 2);
  }

  int aiMaxSpeed = _max(4, maxBallXVel - 1) + random(-2,2);
  int aiDeadzone = aiMaxSpeed + 2; // to give it a bit of room
  
  int deltaY = aiTargetY - paddles[1].y;

  if (abs(deltaY) > aiDeadzone) {
    paddles[1].vy = (deltaY > 0) ? aiMaxSpeed : -aiMaxSpeed;
  } else {
    paddles[1].vy = 0;
  }
}

void reflectBallOffPaddle(Paddle p) {
  float impact = (float)(ball->y - (p.y + p.h / 2)) / (paddles[0].h / 2);
  ball->vx = (ball->vx > 0 ? -maxBallXVel : maxBallXVel);
  float spikeMultiplier = 1.6;
  ball->vy = impact * maxBallXVel * spikeMultiplier;
}
