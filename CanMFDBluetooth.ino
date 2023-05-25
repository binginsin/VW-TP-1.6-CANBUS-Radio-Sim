#include <SPI.h>
#include <mcp2515.h>
#include <cppQueue.h>
#include <BM83.h>
//#include <Snooze.h>
//
//SnoozeDigital digital;
//
//// configures the lc's 5v data buffer (OUTPUT, LOW) for low power
//Snoozelc5vBuffer  lc5vBuffer;
//SnoozeBlock config_teensyLC(lc5vBuffer, digital);

MCP2515 mcp2515(10);

typedef struct can_frame canFrame;

const canid_t CanId = 0x684;

const canid_t OwnedCanId = 0x685;

const canid_t OwnedRingId = 0x43A;

const canid_t OwnedKeepAliveId = 0x665;

const canid_t ButtonCanId = 0x5C1;

canid_t PreviousCanId = 0x439;

canid_t NextCanId = 0x42B;

const canid_t MasterCanId = 0x42B;

bool _started = false;

bool _canActive = false;

enum DisplayMode {
	None,
	Settings,
	SingleTrack,
	MultiTrack
};

// ------------------- OPS ------------------

#define WaitingWithMore 0
#define WaitingLast 1
#define NotWaitingWithMore 2
#define NotWaitingLast 3
#define AckReady 11
#define AckNotReady 9
#define Channel 10

// ------------------- Operation codes --------------------

#define OUT_ADD_MFA 0x02
#define OUT_DRAW 0x09
#define IN_SCREENAREA 0x23 //0x23 can also mean Broadcast.
#define IN_REQTOJUMP 0x2A
#define UP_ARROW 0x22
#define DOWN_ARROW 0x23

/*When the ring is stationary, the following messages are generated cyclically :
D0 D1 D2 D3 D4 D5
420 + ID DLC 6 : xx 01 00 00 00 00


D0 : xx = ID of the following device in ring
D1 : Status byte : 0x01 ==> ring active
0x02 ? ? ? Change in the ring, or new init ring
0x11 ==> device is ready for sleep mode ? ?
(Important for the emulation, i.e.when the ignition is off, the telephone module should send the status 0x11, otherwise the CAN bus will not come to rest)
*/

//Canbus sleep message seems to be:
//42B 6 B 14 0 0 0 0 
//And wake message seems to be:
//42B 6 B 2 C0 2 0 0 

//It seems that radio registers with the MFD before it has fully joined the ring, which brings us to the question - do we really need to join the ring?
//On ignition startup we can see this sequence with the radio:
//42B      6 0B 02 00 40 00 00 //means rebuild ring - if we see this we probably need to request to login
//439      6 19 02 80 00 00 00 //Radio requests to login
//42B      6 19 01 00 00 00 00
//439      6 0B 01 00 00 00 00  

//On ignition turn off we see this sequence:
//439      6 0B 11 00 00 00 00 //ready to sleep - repeated 66 times (every 90ms  - this gets repeated instead of the normal keep alive
//42B      6 19 11 00 00 00 00 //ready to sleep
//439      6 0B 31 00 00 00 00 //is this a response?? not documented anywhere afaik
// ---------- Default Communication Parameters ------------

uint8_t _blockSize = 4;
uint8_t _msBetweenPackets = 5;

uint16_t _timeout = 100;

const long _keepAlive = 1000;

// ----------------------Variables -----------------------

uint8_t _mainMenuId = 0x00;

uint8_t _settingsMenuId = 0x00;

unsigned long _previousMillis = 0;

unsigned long _previousMillisKeepAlive = 0;

unsigned long _ringKeepAliveMillis = 0;

uint8_t _frameInBlock = 1;

char _menuName[10] = "Bluetooth";

char _settingsMenuName[10] = "Bluetooth";

bool _ringJoined;

bool _channelInitialized;

bool _thisPageActive = false;

Queue  _frameQueue(sizeof(canFrame), 20, FIFO); // Instantiate queue

DisplayMode _mode = None;

DisplayMode _lastMode = SingleTrack;

bool _settingsMenuAdded = false;

bool _mainMenuAdded = false;

uint8_t _sequenceNumber = 0;

// ----------------------- SONG INFO -----------------------

struct SongInfo {

	char artist[20];

	char title[20];
};

SongInfo _currentSong = {"Use arrows", "To change songs" };

SongInfo _songQueue[20];


// ---------------------------- BM83 --------------------------
BM83 BT(&Serial3); //RX = 7, TX = 8 


//TODO
//Need to setup an INPUT pin that will listen for a HIGH signal. 
//When signal is HIGH, we are live and running, but not necessarily active on CAN - we can still run initialize can as it will init and disconnect after a few seconds.
//When signal is LOW, we need to turn off bluetooth and put teensy into low power mode
void setup() {
	//0x680 to dec = 1664
	//Idea:
	//Req:  684 01 C0 80 06 85 06 01
	//Resp: 685 00 D0 85 06 80 06 01
	//Req:  684 A0 04 8A FF 32 FF
	//Resp: 685 A1 04 8A FF 32 FF
	//Req:  684 20 02 70 52 12 (41 75 64) - ASCII:  pR Aud - 0x52  Radio (requester) wants to  - 0x20 - 2 means not waiting for ack, more packets to follow. 0 is the sequence number.
	//684 21 (69 6F) 00 00 00 00 00 - ASCII:!io      - 0x02 70 add menu to MFA+          - 0x21 - 2 means not waiting for ack, more packets to follow. 1 is the sequence number.
	//684 A3                                         - 0x12  in the middle segment  (110x91 pixels) - this is request of channel info.
	//684 12 00 00 00 00 00 00                       - with title 'Audio'                - 0x12 - 1 means waiting for ack, this is the last packet. 2 is the sequence number.
	//Resp: 685 A1 04 8A FF 32 FF         - Response to A3
	//685 B3                                                                             - 0xB3 - B means ack. 3 is the sequence number.
	//685 10 23 00 00 00            - 0x23 0x00  reply with screen area ID, 0x00 in this example  - 0x10 - 1 means waiting for ack, this is the last packet. 0 is sequence no.
	//Req:  684 B1                                                                             - 0xB1 - B means ack, 1 is the sequence number.


	//To put TJA1055T to sleep, from what I gather we need to setup STB and EN pins as output, have them high on normal operation.
	//When we are ready to sleep, send LOW on STB, then a little later pull EN LOW
	//When we are ready to get back to normal, pull both high (or pull STB high first then EN high)
	//We can put the MCP2515 to sleep by using mcp2515.setSleepMode()
	//Probably can bring it back online with setNormalMode();
	//digital.pinMode(21, INPUT_PULLUP, RISING);
	while (!Serial);
	Serial.begin(115200);
	

	mcp2515.reset();
	mcp2515.setBitrate(CAN_100KBPS);
	mcp2515.setConfigMode();

	uint32_t mask0 = 0x7E0; // 0x7E0 will match the first 6 bits of the 11 bit identifier
	uint32_t mask1 = 0x7FF; // 0x7FF Matches a short identifier(exact).
	uint32_t filter0 = 0x680; //will match 680-69F
	uint32_t filter1 = 0x420; //will match 420-43F
	uint32_t filter2 = ButtonCanId;
	mcp2515.setFilterMask(MCP2515::MASK0, false, mask0);
	mcp2515.setFilter(MCP2515::RXF0, false, filter0);
	mcp2515.setFilter(MCP2515::RXF1, false, filter1);
	mcp2515.setFilterMask(MCP2515::MASK1, false, mask1);
	mcp2515.setFilter(MCP2515::RXF2, false, filter2);
	mcp2515.setFilter(MCP2515::RXF3, false, 0x000);
	mcp2515.setFilter(MCP2515::RXF4, false, 0x000);
	mcp2515.setFilter(MCP2515::RXF5, false, 0x000);

	mcp2515.setNormalMode();

	Serial.println("MFD CANBUS");
	Serial.println("Type start to start or stop to stop");

	InitializeCan();

}

void InitializeBluetooth()
{
	BT.begin(9600);
	BT.SubscribeElementAttributesReceived(ElementAttributesReceived);
	turnOnModule();
}

void ElementAttributesReceived(char* title, char* artist, char* genre, char* album, char* trackNo, char* totalTracks, char* playTime)
{
	Serial.println(artist);
	Serial.println(title);
	Serial.println(album);
	Serial.println(genre);
	strncpy(_currentSong.artist, artist, 19);
	strncpy(_currentSong.title, title, 19);
	DisplaySingleTrack();
}

void turnOnModule()
{
	//Serial.println("Querying status");
	//BM83::UartCommand status = BT.GetStatus();
	//if (BT.PowerState == Off)
	//{
	Serial.println("Sending On Command");
	BT.PowerOn();
	//	delay(10);
	//}
	//BT.subscribeAVRCPEvents();
	BT.RequestStatus();
	BT.SubscribeAVRCPTrackChanged();
}

void InitializeCan()
{
	while (!_started)
	{
		String serialString = "";
		if (Serial.available())
		{
			serialString = Serial.readString();
			Serial.println(serialString);
			serialString.trim();
			if (serialString == "start")
			{
				Serial.println("STARTED");
				_started = true;
				break;
			}
			else if (serialString == "stop")
			{
				_started = false;
			}
		}
	}

	if (_started)
	{
		_channelInitialized = false;
		_canActive = true;
		//Maybe we don't even need to be on the ring... Let's try it without joining the ring
		/*_ringJoined = false;
		if (!JoinRing())
		{
			Serial.println("Failed to join ring");
			return;
		}
		RingKeepAlive(millis());*/
		if (InitializeChannel())
		{
			RingKeepAlive(millis());
			//if(RegisterWithScreenArea())
			//{
			  //RingKeepAlive();
			AddMainMenu();
			if (!_mainMenuAdded)
			{
				return;
			}
			RingKeepAlive(millis());
			KeepAlive(millis());
			//}
			AddSettingsMenu();
			if (!_settingsMenuAdded)
			{
				Log("Settings menu didn't add. Continuing");
			}
		}

		//InitializeBluetooth();
	}
}

long _prevMemMillis = millis();

void loop() {

	String serialString = "";
	if (Serial.available())
	{
		serialString = Serial.readString();
		serialString.trim();
		if (serialString == "start" && !_started)
		{
			Serial.println("STARTED");
			_started = true;
			InitializeCan();
		}
		else if (serialString == "stop")
		{
			_started = false;
			DisconnectChannel();
		}
		else
		{
			__u8 splitLoc = serialString.indexOf("-");
			serialString.substring(0, splitLoc).toCharArray(_currentSong.artist, 19);
			serialString.substring(splitLoc + 1, serialString.length()).toCharArray(_currentSong.title, 19);
			DisplaySingleTrack();
		}
	}
	if (_started)
	{
		unsigned long currentMillis = millis();
		canFrame canMsg = {};

		RingKeepAlive(currentMillis);

		if (_msBetweenPackets > 0 && currentMillis - _previousMillis >= (_msBetweenPackets * 2)) {
			SendNextMessage();
			_previousMillis = currentMillis;
		}

		KeepAlive(currentMillis);

		if (mcp2515.readMessage(&canMsg) == MCP2515::ERROR_OK)
		{
			PrintMessage(&canMsg, false);
			AckIfReq(canMsg);
			DecodeFrame(canMsg, false);
		}
	}

	/*if (millis() - _prevMemMillis > 1000)
	{
		Serial.println(FreeMem());
		_prevMemMillis = millis();
	}*/
	//BM83::UartCommand command = {};
	//BT.getIncomingCommand(command);
	//BT.DecodeCommand(command);

	//if (digitalRead(21) == HIGH)
	//{
	//	Serial.println("Entering Hibernation mode");
	//	//TODO put MCP and TJA to sleep
	//	Snooze.hibernate(config_teensyLC);
	//}
}

uint32_t FreeMem() { // for Teensy 3.0
	uint32_t stackTop;
	uint32_t heapTop;

	// current position of the stack.
	stackTop = (uint32_t)&stackTop;

	// current position of heap.
	void* hTop = malloc(1);
	heapTop = (uint32_t)hTop;
	free(hTop);

	// The difference is (approximately) the free, available ram.
	return stackTop - heapTop;
}
