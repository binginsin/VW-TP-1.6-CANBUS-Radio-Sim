// When we request to login but don't do anything else
// 43A      6 1A 02 80 00 00 00
// 439      6 19 02 80 00 00 00
// 42B      6 0B 02 00 40 00 00
// 439      6 0B 01 00 00 00 00
// 42B      6 19 01 00 00 00 00

// Need to try with
// 0x02, 0xC0, 0x04, 0x00, 0x00

// From what I can see on the CAN comms, if we just send the keep alive without even asking to join the ring, the radio seems to accept us... Weird

// Initializes the channel and reads the channel settings.
bool JoinRing()
{
	uint32_t retryCount = 0;
	// could also be
	canFrame frame = {OwnedRingId, 6, OwnedRingId - 0x420, 0x02, 0xC0, 0x04, 0x00, 0x00};
	// 0x02 means rebuild the ring
	// 0x80 means request to login
	// canFrame frame = { OwnedRingId, 6, OwnedRingId - 0x420, 0x02, 0x80, 0x00, 0x00, 0x00 };
	// You should see a series of packets with a format like:

	//  (0x436) 16 02 C0 04 00 00 (6 bytes)
	//	(0x428) 16 01 00 00 00 00 (6 bytes)
	//	(0x436) 08 01 00 00 00 00 (6 bytes)

	//	The first byte in each packet matches the frameID(420 + byte0).

	//	Essentially that series of packets equates to :

	//  ID 436 asking to join the ring
	//	ID 428 is the last device in the ring saying it's OK to join
	//	ID 436 responds back to ID 428 acknowledging the message

	// Expect master to respond first
	// TODO need to double check this with more than 1 device available!!
	// I don't believe master will be responding with 0x2 as that means rebuild the ring, but maybe ring is rebuilt every time a device joins idk.
	/*canframe expectedresponse = { mastercanid, 2, 0x0b, 0x02 };
	canframe response = waitforresponse(frame, expectedresponse, true);
	_timeout = 1000;
	while (response.can_id == 0x000 && retrycount < 10000)
	{
		response = waitforresponse(frame, expectedresponse, true);
	}*/

	// send nothing and wait for another response from the following device in the ring
	// currently hardcoded to MasterCanId, but it shouldn't be. May be suitable for my uses though
	// frame = { };
	canFrame expectedResponse = {PreviousCanId, 2, OwnedRingId - 0x420, 0x01};
	canFrame response = WaitForResponse(frame, expectedResponse, true, false);
	_timeout = 1000;
	if (response.can_id != 0x000)
	{
		PreviousCanId = response.can_id;
		Serial.print("RING JOINED, previous can ID ");
		Serial.println(PreviousCanId, HEX);
		_ringJoined = true;
		_timeout = 100;
		return true;
	}
	_timeout = 100;
	return false;
}

bool LeaveRing()
{
	return false;
}

bool InitializeChannel()
{
	canFrame frame = {CanId, 6, 0xA0, 0x04, 0x59, 0xFF, 0x32, 0xFF};
	canFrame response = WaitForResponseMin(frame, OwnedCanId, 0xA1);
	while (response.can_id == 0x000)
	{
		response = WaitForResponseMin(frame, OwnedCanId, 0xA1);
	}
	ChannelParams(response);
	return true;
}

void DisconnectChannel()
{
	canFrame frame = {CanId, 1, 0xA8};
	canFrame response = WaitForResponseMin(frame, OwnedCanId, 0xA8);
	uint8_t attemptCtr = 0;
	while (response.can_id == 0x000 && attemptCtr < 5)
	{
		attemptCtr++;
		response = WaitForResponseMin(frame, OwnedCanId, 0xA8);
	}

	if (attemptCtr >= 5)
	{
		Serial.println("Failed to disconnect from MFD channel");
	}
	else
	{
		Serial.println("Disconnected from MFD channel successfully");
	}
}

void KeepAlive(unsigned long currentMillis)
{
	if (_canActive && currentMillis - _previousMillisKeepAlive >= _keepAlive)
	{
		_previousMillisKeepAlive = currentMillis;
		canFrame frame;
		if (_channelInitialized)
		{
			frame.can_id = CanId;
			frame.can_dlc = 1;
			frame.data[0] = 0xA3;
			void SendFrame(canFrame * frame);
			PrintMessage(&frame, true);
		}

		// Radio seems to be sending 0x83 always
		if (!_mainMenuAdded)
		{
			frame = {OwnedKeepAliveId, 8, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		}
		else
		{
			frame = {OwnedKeepAliveId, 8, 0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
		}
		PrintMessage(&frame, true);
		mcp2515.sendMessage(&frame);
	}
}

void RingKeepAlive(unsigned long currentMillis)
{
	if (_canActive && currentMillis - _ringKeepAliveMillis >= 60)
	{
		_ringKeepAliveMillis = currentMillis;
		if (_ringJoined)
		{
			canFrame frame = {OwnedRingId, 6, 0x0B, 0x01, 0x00, 0x00, 0x00, 0x00};
			mcp2515.sendMessage(&frame);
			PrintMessage(&frame, true);
		}
	}
}

// Parses and saves channel parameters from the received packet
void ChannelParams(canFrame packet)
{
	// Received channel info
	_blockSize = packet.data[1];
	uint8_t units = packet.data[4] & 0xC0;
	uint8_t scale = packet.data[4] & 0x3F;
	uint8_t timeoutUnits = packet.data[2] & 0xC0;
	uint8_t timeoutScale = packet.data[2] & 0x3F;
	// 0x00 not supported here
	if (units == 0x00)
	{
		_msBetweenPackets = scale / 10;
	}
	else if (units == 0x01)
	{
		_msBetweenPackets = scale;
	}
	else if (units == 0x02)
	{
		_msBetweenPackets = scale * 10;
	}
	else if (units == 0x03)
	{
		_msBetweenPackets = scale * 100;
	}

	if (timeoutUnits == 0x00)
	{
		_timeout = timeoutScale / 10;
	}
	if (timeoutUnits == 0x01)
	{
		_timeout = timeoutScale;
	}
	else if (timeoutUnits == 0x02)
	{
		_timeout = timeoutScale * 10;
	}
	else if (timeoutUnits == 0x03)
	{
		_timeout = timeoutScale * 100;
	}

	// When we are re-initializing the channel
	if (!_channelInitialized)
	{
		_sequenceNumber = 0;
	}

	_channelInitialized = true;
	Serial.println("CHANNEL INITIALIZED");
	Serial.print("Time between packets: ");
	Serial.print(_msBetweenPackets);
	Serial.println();
	Serial.print("Packet timeout: ");
	Serial.print(_timeout);
	Serial.println();
}

void DecodeFrame(canFrame packet, bool allowOnlyRingMessages)
{
	uint8_t OP = packet.data[0] >> 4;
	uint8_t SEQ = packet.data[0] & 0x0F;

	if (OP >= WaitingWithMore && OP <= NotWaitingLast)
	{
		if (packet.can_id == OwnedCanId && !allowOnlyRingMessages)
		{
			switch (packet.data[1])
			{
			case IN_SCREENAREA:
				if (packet.data[3] == 0x00)
				{
					if (packet.data[2] == _mainMenuId)
					{
						_lastMode = _mode;
						_mode = None;
					}
					else if (packet.data[2] == _settingsMenuId)
					{
						_mode = None;
					}
				}
				else if (packet.data[3] == 0x01)
				{
					if (packet.data[2] == _mainMenuId)
					{
						if (_lastMode == None)
						{
							_lastMode = SingleTrack;
						}
						_mode = _lastMode;
						DisplaySingleTrack();
					}
					else if (packet.data[2] == _settingsMenuId)
					{
						_mode = Settings;
					}
				}
				break;
			case IN_REQTOJUMP:
				// ignore
				/*if(packet.can_id != OwnedCanId)
				  {
				  _thisPageActive = false;
				  return;
				  }*/
				break;
			}
		}
		else if (packet.can_id == ButtonCanId && !allowOnlyRingMessages)
		{
			switch (packet.data[1])
			{
			case UP_ARROW:
				if (_mode == SingleTrack) // || mode == MultiTrack
				{
					Serial.println("Next track");
					// TODO send a command off to bluetooth for next track
					BT.MusicNextTrack();
				}
				break;
			case DOWN_ARROW:
				if (_mode == SingleTrack) // || mode == MultiTrack
				{
					Serial.println("Previous track");
					// TODO send a command off to bluetooth for previous track
					BT.MusicPreviousTrack();
				}
			}
		}
		else if (packet.can_id == PreviousCanId)
		{
			// TODO look at 0x11 as well
			// seen 0x11 mentioned in many place to mean sleep. We need to reply with 0x11 as well
			if (packet.data[1] == 0x11)
			{
				// Send sleep and sleep bluetooth
				// would be good to see exactly what the radio does in this scenario
				Log("Ready to sleep");
				//_started = false;
				_canActive = false;
				DisconnectChannel();
			}
			else if (packet.data[1] == 0x02)
			{
				// Join ring and wake bluetooth
				Log("Need to rejoin ring now");
				_canActive = true;
				InitializeCan();
				//_started = true;
			}
		}
		else if (packet.can_id == MasterCanId)
		{
			if (packet.data[1] == 0x31)
			{
				Log("Ready to sleep by master");
				_started = false;
				_canActive = false;
				DisconnectChannel();
			}
		}
		// else if (packet.can_id == PreviousRingDeviceId)
		//{
		//	if (packet.data[1] == 0x11)
		//	{
		//		//Send sleep and sleep bluetooth
		//		//would be good to see exactly what the radio does in this scenario
		//		Log("Ready to sleep");
		//	}
		//	else if (packet.data[1] == 0x02 && packet.data[2] == 0xC0 && packet.data[3] == 0x02)
		//	{
		//		//Join ring and wake bluetooth
		//		Log("Need to rejoin ring now");
		//	}
		// }
	}
}

void ClearMainMenuScreenFramesForText(canFrame *clearScreenFrames, __u8 screenId, __u8 x, __u8 y, __u8 width, __u8 height)
{
	clearScreenFrames[0] = {CanId, 8, (__u8)(0x20 + SequenceNumber()), 0x09, screenId, 0x60, 0x09, 0x00, x, 0x00};
	clearScreenFrames[1] = {CanId, 8, (__u8)(0x20 + SequenceNumber()), y, 0x00, width, 0x00, height, 0x00, 0x61};
}

void AddBytesToFrames(canFrame *frames, __u8 framesLength, byte *bytes, uint8_t dataLength, __u8 *locationInFrame, __u8 *frameCount)
{
	uint8_t j = 0;
	for (uint8_t i = *frameCount; i < framesLength; i++)
	{
		if (*locationInFrame == 1)
		{
			frames[*frameCount] = {CanId, 8, (__u8)(0x20 + SequenceNumber())};
		}
		for (; j < dataLength; j++)
		{
			if (*locationInFrame > 7)
			{
				(*frameCount)++;
				*locationInFrame = 1;
				break;
			}
			frames[*frameCount].data[(*locationInFrame)++] = bytes[j];
		}
		if (j == dataLength)
		{
			break;
		}
	}
}

void DisplaySingleTrack()
{
	if (_mode != SingleTrack)
	{
		return;
	}
	// We have to carry on with the sequence number from before. After adding both menus it will be at 4 now
	//_sequenceNumber = seq;

	canFrame frames[14];
	ClearMainMenuScreenFramesForText(frames, _mainMenuId, 0, 0, 110, 91);
	// 0x61 is by default in clear screen frames
	// Set parameters for artist
	size_t artistLength = strlen(_currentSong.artist);
	frames[2] = {CanId, 8, (__u8)(0x20 + SequenceNumber()), (__u8)(artistLength + 6), 0x14, 0x00, 55, 0x00, 20, 0x00};

	// Set parameters for song title
	size_t titleLength = strlen(_currentSong.title);
	__u8 titleParams[8] = {0x61, (__u8)(titleLength + 6), 0x14, 0x00, 55, 0x00, 40, 0x00};

	uint8_t locationInFrame = 1;
	uint8_t frameCount = 3;

	uint8_t frameSizeTotal = sizeof(frames) / sizeof(canFrame);

	// Fill up the frame buffer with data and parameters
	AddBytesToFrames(frames, frameSizeTotal, (byte *)_currentSong.artist, artistLength, &locationInFrame, &frameCount);
	AddBytesToFrames(frames, frameSizeTotal, titleParams, 8, &locationInFrame, &frameCount);
	AddBytesToFrames(frames, frameSizeTotal, (byte *)_currentSong.title, titleLength, &locationInFrame, &frameCount);

	Serial.println(frameCount);
	Serial.println(locationInFrame);

	// Add trailing zeroes to pad the last frame
	for (uint8_t k = locationInFrame; k < 8; k++)
	{
		frames[frameCount].data[k] = 0x00;
	}

	// Send the frames, EnqueueFrame here sends 4 frames and then waits for ACK
	// If we are taking up 7 or less bytes of the frame, we can still add 0x08 at the end
	if (locationInFrame < 8)
	{
		for (uint8_t i = 0; i < frameCount; i++)
		{
			bool result = EnqueueFrame(frames[i], i + 1);
			if (!result)
			{
				Serial.println("Failed to display single track");
				return;
			}
		}

		// make sure last frame is acked and 0x08 signals end of message
		frames[frameCount].data[0] -= 0x10;
		frames[frameCount].can_dlc = locationInFrame + 1;
		frames[frameCount].data[locationInFrame] = 0x08;
	}
	// If we are taking up all bytes of the frame, we have to create a new frame to signal end of message
	else
	{
		for (uint8_t i = 0; i <= frameCount; i++)
		{
			bool result = EnqueueFrame(frames[i], i + 1);
			if (!result)
			{
				Serial.println("Failed to display single track");
				return;
			}
		}

		frameCount++;
		frames[frameCount].can_id = CanId;
		frames[frameCount].can_dlc = 2;

		frames[frameCount].data[0] = (__u8)(0x10 + SequenceNumber());
		frames[frameCount].data[1] = 0x08;
	}

	// Send last frame and wait for ack
	canFrame expectedResponse = {OwnedCanId, 4, 0x00, 0x27, _mainMenuId, 0x01};
	canFrame response = WaitForResponse(frames[frameCount], expectedResponse, false, true);
	if (response.can_id == 0x000)
	{
		Serial.println("Failed to display single track");
		return;
	}
	Serial.println("Single track displayed");
}

void HandleDisplayFailure()
{
	switch (_mode)
	{
	case SingleTrack:
	case MultiTrack:

		break;
	}
}

void UnregisterDisplayArea(__u8 areaId)
{
	// 680      3 12 05 00
}

void DisplaySettings()
{
	// figure out how to show the arrows next to selected items, i think there was an easy option to do that.
	// where:
	// 60 - command
	// ll - data length in the command after this byte, always 09
	// aa - attribute: 0 or 1 - erase, 2 - draw, 3 - draw cursor.The cursor is always drawn the same way, regardless of the widthand height of the rectangle.
	// 00 - delimiters
	// xx - X coordinate, in pixels. 0 upper left corner of the screen area to which rendering is performed.
	// yy - Y coordinate
	// ww - width
	// hh - height
	// To avoid errors, the Xand Y coordinates must be within the screen area, widthand height can go beyond.
}

__u8 SequenceNumber()
{
	if (_sequenceNumber == 16)
	{
		_sequenceNumber = 0;
	}
	return _sequenceNumber++;
}

void AckIfReq(canFrame packet)
{
	__u8 OP = packet.data[0] >> 4;
	__u8 SEQ = packet.data[0] & 0x0F;
	if (packet.can_id == OwnedCanId && (OP == WaitingWithMore || OP == WaitingLast))
	{
		SendAck(SEQ);
	}
}

bool EnqueueFrame(canFrame frame, uint8_t frameCount)
{
	if (frameCount % 4 == 0)
	{
		__u8 OP = frame.data[0] & 0xF0;
		__u8 SEQ = frame.data[0] & 0x0F;
		if (OP >= 0x20)
		{
			frame.data[0] -= 0x20;
		}
		// We are not rolling over SEQ here.
		// We send a packet with Sequence number F and expect F+1 LOL
		// Also seems like MFD sometimes sends the wrong SEQ :S Not sure why or how to read that
		__u8 expectedOpSeq = 0xB0 + SEQ + 1;
		if (SEQ == 0x0F)
		{
			expectedOpSeq = 0xB0;
		}
		canFrame expectedResponse = {OwnedCanId, 1, expectedOpSeq};
		canFrame response = WaitForResponse(frame, expectedResponse, true, true);
		if (response.can_id != 0x000)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	_frameQueue.push(&frame);
	return true;
}

void SendNextMessage()
{
	canFrame frame;
	if (!_frameQueue.isEmpty())
	{
		_frameQueue.pop(&frame);
		/*if(frame.data[0] >> 4 != 0xA)
		  {
		  if (_frameInBlock == _blockSize)
		  {
			_frameInBlock = 0;
			if(frame.data[0] >= 0x20)
			{
			   frame.data[0] -= 0x20;
			}
			//In theory at this point we should reset the count of waiting for ACK from MFD, however for the time being we will basically ignore ACKS
		  }
		  _frameInBlock++;
		  }*/
		PrintMessage(&frame, true);
		mcp2515.sendMessage(&frame);
	}
}

// TODO need to de-register with 05 xx (05 09 or 05 10)
// Radio example:
// 680      3 12 05 00
// 681      1 B3
// 681      3 11 25 00
// 680      1 B2
// Even after de-register we should be sending A3 keep alives if we are on(hibernate)
void AddMainMenu()
{
	Serial.println("Adding main menu");
	// Add menu command will have 2 packets
	// 52 - Radio, Navi - 4D, Phone - 5A, Telematics - 55, Compass - 40
	// 07 - area in the SETTINGS settings menu. 12 - area in the main menu. The answer will be a message with dimensions 110x166 pixels - 21 00 3F 00 6E 00 A6, 12 - the central area of ​​the screen. The answer with dimensions 110x91 pixels is 21 00 04 00 6E 00 5B, 23 - the area at the top of the screen. The answer with dimensions of 45x20 pixels is 21 00 03 00 2D 00 14

	canFrame message = {CanId, 8, (__u8)(0x20 + SequenceNumber()), 0x02, 0x70, 0x5A, 0x12, _menuName[0], _menuName[1], _menuName[2]};
	EnqueueFrame(message, 1);

	canFrame message1 = {CanId, 8, (__u8)(0x10 + SequenceNumber()), _menuName[3], _menuName[4], _menuName[5], _menuName[6], _menuName[7], _menuName[8], _menuName[9]};

	canFrame expectedResponse = {OwnedCanId, 2, 0x10, 0x23, 0x00, 0x00, 0x00}; // can be 10 23 08 00 00 or 10 23 09 00 00 or 10 23 0A 00 00 for top right area (dlc is set to 2 here, so that we only check the first 2 bytes. We dont check if expected.dlc == response.dlc
	canFrame response = WaitForResponse(message1, expectedResponse, false, true);
	if (response.can_id != 0x000)
	{
		_mainMenuId = response.data[2];
		Serial.print("Main menu section added with id ");
		Serial.println(_mainMenuId);
		_mainMenuAdded = true;
	}
	else
	{
		Serial.println("MAIN MENU ADD FAILED");
	}
}

void AddSettingsMenu()
{
	Serial.println("Adding settings menu");

	canFrame message = {CanId, 8, (__u8)(0x20 + SequenceNumber()), 0x02, 0x70, 0x5A, 0x07, _menuName[0], _menuName[1], _menuName[2]};
	EnqueueFrame(message, 1);

	canFrame message1 = {CanId, 8, (__u8)(0x10 + SequenceNumber()), _menuName[3], _menuName[4], _menuName[5], _menuName[6], _menuName[7], _menuName[8], _menuName[9]};

	canFrame expectedResponse = {OwnedCanId, 2, 0x10, 0x23, 0x00, 0x00, 0x00};
	canFrame response = WaitForResponse(message1, expectedResponse, false, true);
	if (response.can_id != 0x000)
	{
		_settingsMenuId = response.data[2];
		Serial.print("Settings menu section added with id ");
		Serial.println(_settingsMenuId);
		_settingsMenuAdded = true;
	}
	else
	{
		Serial.println("SETTINGS MENU ADD FAILED");
	}
}

void Log(String message)
{
	Serial.println(message);
}

// The identifiers of the screen areas will be different for each of the 5 connections, 3:
// 00, 01, 02 for Radio
// 10, 11, 12 for Navi
// 08, 09, 0A for Phone
// 18, 19, 1A for Telematics
// 20, 21, 22 for Compass

void sendUpTo(canFrame frame)
{
	if (_frameQueue.isEmpty())
	{
		return;
	}
	canFrame currentInQueue;
	canFrame message;
	_frameQueue.peek(&currentInQueue);

	while (currentInQueue != frame)
	{
		unsigned long currentMillis = millis();

		RingKeepAlive(currentMillis);

		KeepAlive(currentMillis);

		if (_msBetweenPackets > 0 && currentMillis - _previousMillis >= (_msBetweenPackets * 2))
		{
			SendNextMessage();
			_previousMillis = currentMillis;
		}

		// don't read when sending
		// if (mcp2515.readMessage(&message) == MCP2515::ERROR_OK)
		//{
		//	PrintMessage(&message);
		//	AckIfReq(message);
		//	//Maybe place in a queue and decode on next loop? As this could cause out of order frames to be sent..
		//	DecodeFrame(message);
		// }

		if (!_frameQueue.peek(&currentInQueue))
		{
			break;
		}
	}
}

bool FrameNotEquals(canFrame frame1, canFrame frame2, bool checkOP, bool checkId)
{
	if (checkId && frame1.can_id != frame2.can_id)
	{
		return true;
	}
	else
	{
		uint8_t i = 0;
		if (!checkOP)
		{
			i = 1;
		}
		for (; i < frame2.can_dlc; i++)
		{
			if (frame1.data[i] != frame2.data[i])
			{
				return true;
			}
		}
	}

	return false;
}

canFrame WaitForResponse(canFrame frame, canFrame expectedResponse, bool checkOP, bool checkId)
{
	canFrame response = {};

	EnqueueFrame(frame, 1);

	// Waits for all packets that were in queue before this packet to be sent out
	sendUpTo(frame);

	unsigned long begginingMillis = millis();
	unsigned long currentMillis;

	while (true)
	{
		response = {};
		currentMillis = millis();
		if (_msBetweenPackets > 0 && currentMillis - _previousMillis >= (_msBetweenPackets * 2))
		{
			SendNextMessage();
			_previousMillis = currentMillis;
		}

		RingKeepAlive(currentMillis);
		KeepAlive(currentMillis);

		if (mcp2515.readMessage(&response) == MCP2515::ERROR_OK)
		{
			PrintMessage(&response, false);
			AckIfReq(response);
			if (FrameNotEquals(response, expectedResponse, checkOP, checkId))
			{
				DecodeFrame(response, true);
			}
			else
			{
				break;
			}
		}

		if (currentMillis - begginingMillis >= (uint16_t)(_timeout + 10))
		{
			response.can_id = 0x000;
			break;
		}
	}

	return response;
}

canFrame WaitForResponseMin(canFrame frame, canid_t canId, __u8 opCode)
{
	canFrame response = {};

	EnqueueFrame(frame, 1);

	// Waits for all packets that were in queue before this packet to be sent out
	sendUpTo(frame);

	unsigned long begginingMillis = millis();
	unsigned long currentMillis;

	while (true)
	{
		response = {};
		currentMillis = millis();
		if (_msBetweenPackets > 0 && currentMillis - _previousMillis >= (_msBetweenPackets * 2))
		{
			SendNextMessage();
			_previousMillis = currentMillis;
		}

		RingKeepAlive(currentMillis);
		KeepAlive(currentMillis);

		if (mcp2515.readMessage(&response) == MCP2515::ERROR_OK)
		{
			PrintMessage(&response, false);
			// Send ack if it's required
			AckIfReq(response);
			// If response is not the expected response, see what else it could be
			if (response.can_id != canId || response.data[0] != opCode)
			{
				DecodeFrame(response, true);
			}
			else
			{
				break;
			}
		}

		if (currentMillis - begginingMillis >= (uint16_t)(_timeout + 10))
		{
			response.can_id = 0x000;
			break;
		}
	}
	return response;
}

void PrintMessage(canFrame *message, bool send)
{
	if (send)
	{
		Serial.print("S: ");
	}
	else
	{
		Serial.print("R: ");
	}
	Serial.print(message->can_id, HEX); // print ID
	Serial.print(" ");
	Serial.print(message->can_dlc, HEX); // print DLC
	Serial.print(" ");

	for (int i = 0; i < message->can_dlc; i++)
	{ // print the data
		Serial.print(message->data[i], HEX);
		Serial.print(" ");
	}
	Serial.println();
}

void SendAck(__u8 sequenceNumber)
{
	__u8 seq = 0;
	if (sequenceNumber < 15)
	{
		seq = sequenceNumber + 1;
	}
	canFrame message;
	message.can_id = CanId;
	message.can_dlc = 1;
	message.data[0] = 0xB0 + seq;

	delay(_msBetweenPackets * 2);
	_previousMillis = millis();
	mcp2515.sendMessage(&message);
	PrintMessage(&message, true);
}
