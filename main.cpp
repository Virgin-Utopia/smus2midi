#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <inttypes.h>
#include <cxxmidi/converters.hpp>
#include <cxxmidi/file.hpp>

typedef struct {
	uint8_t sID;      /* SEvent type code                             */
	uint8_t data;     /* sID-dependent data                           */
} SEvent;


typedef struct {
	uint32_t time; //ABSOLUTE Time
	uint8_t data;//pitch or program number
	uint8_t velocity;
	bool tied;
	bool progChange;
} MidiEvent;

bool MidiEventCompare(const MidiEvent& i, const MidiEvent& j) { return (i.time < j.time); }

class AMidiTrack {
public:
	std::string trackName;
	CxxMidi::Track* pTrack;
	int trackNumber;
	int programNumber;
	bool init;
	uint32_t time;
};

bool operator==(AMidiTrack const& a, AMidiTrack const& b) {
	return a.trackName == b.trackName;
}
int getMidiTrackFromProgram(std::vector<AMidiTrack> lstTargetTracks, int trackNumber, int programNumber) {
	int pos = 0;
	bool found = false;

	for (AMidiTrack track : lstTargetTracks) {
		if ((track.trackNumber == trackNumber)&&(track.programNumber == programNumber)) {
			found = true;
			break;
		}
		pos++;
	}
	if (!found) {
		pos = -1;
	}

	return pos;
}

template <typename T>
bool alreadyPresent(std::vector<T>& references, T const& newValue)
{
	bool present = true;
	int results = std::find(references.begin(), references.end(), newValue) - references.begin();
	if (results == references.size()) {
		present = false;
	}
	return present;
}

template <typename T>
int createIndex(std::vector<T>& references, T const& newValue)
{
	int results = std::find(references.begin(), references.end(), newValue) - references.begin();
	if (results == references.size()) {
		references.push_back(newValue);
	}
	return results;
}

void parseSEvent(unsigned char* buffer, uint32_t offset, uint32_t dt, uint32_t& time, uint8_t& velocity, std::vector<MidiEvent>& events, int& currentProgramNumber)
{
	unsigned char sID = buffer[offset];
	if ((0 <= sID) && (128 >= sID))
	{
		//Note event
		bool chord = (buffer[offset + 1] & 0x80);
		bool tieOut = (buffer[offset + 1] & 0x40);
		uint8_t nTuplet = ((buffer[offset + 1] & 0x30) >> 4);
		bool dot = (buffer[offset + 1] & 0x08);
		uint8_t division = (buffer[offset + 1] & 0x07);

		uint32_t duration = dt * (powf(2.0f, 2 - division)) * (dot ? 1.5f : 1.0f);
		if (nTuplet > 0)
			duration *= (2 * nTuplet) / (2 * nTuplet + 1.0f);
		uint32_t nextStart = chord ? 0 : duration;

		if (sID < 128)
		{
			//Note
			MidiEvent noteOn;
			noteOn.time = time;
			noteOn.data = sID;
			noteOn.velocity = velocity;
			noteOn.tied = false;
			noteOn.progChange = false;
			events.push_back(noteOn);

			MidiEvent noteOff;
			noteOff.time = time + (duration * .875);
			noteOff.data = sID;
			noteOff.velocity = 0;//Note Off
			noteOff.tied = tieOut;
			noteOff.progChange = false;
			events.push_back(noteOff);
		}
		else
		{//rest
		}

		time += nextStart;
	}
	else
		switch (sID)
		{
		case 129:
			//set instrument number
		case 134:
			//set midi preset
			
			currentProgramNumber = (int)(buffer[offset + 1]);
			//std::cout << "Program change : '" << currentProgramNumber << "'" << std::endl;
			 
			//send midi programe change with buffer[offset+1]
			MidiEvent progChange;
			progChange.time = time;
			progChange.data = buffer[offset + 1];
			progChange.velocity = 0;
			progChange.tied = false;
			progChange.progChange = true;
			events.push_back(progChange);

			break;
		case 132:
			//set volume
			velocity = buffer[offset + 1];
			break;
		case 130:
			// set time signature
			break;
		case 131:
			// set key signature
			break;
		default:
			std::cout << "* Private SEvent : '" << (int)(sID) << "'" << std::endl;
			break;
		}
}

uint32_t computeLength(unsigned char* buffer, uint32_t offset)
{
	uint32_t length = (buffer[offset] << 24) + (buffer[offset + 1] << 16) + (buffer[offset + 2] << 8) + (buffer[offset + 3] + 8);
	return length;
}

CxxMidi::Event setTrackName(std::string trackName)
{
	CxxMidi::Event e = CxxMidi::Event(0,   // deltatime
		CxxMidi::Message::Meta,			// meta
		CxxMidi::Message::TrackName     // track name
	);

	std::cout << "Set track name with '" << trackName << "'" << std::endl;

	uint8_t len = trackName.length();

	for (int i = 0; i < len; i++)
		e.push_back(trackName[i]);

	return e;
}

CxxMidi::Event setCopyright(std::string copyright)
{
	CxxMidi::Event e = CxxMidi::Event(0,   // deltatime
		CxxMidi::Message::Meta,			// meta
		CxxMidi::Message::Copyright     // copyright
	);

	std::cout << "Set copyright with '" << copyright << "'" << std::endl;

	uint8_t len = copyright.length();

	for (int i = 0; i < len; i++)
		e.push_back(copyright[i]);

	return e;
}

CxxMidi::Event setTempo(uint32_t microsecsPerQuarterNote)
{
	CxxMidi::Event e = CxxMidi::Event(0,   // deltatime
		CxxMidi::Message::Meta,		// meta
		CxxMidi::Message::Tempo     // tempo
	);

	std::cout << "Set track tempo '" << microsecsPerQuarterNote << "'" << std::endl;

	e.push_back((microsecsPerQuarterNote & 0x00ff0000) >> 16);
	e.push_back((microsecsPerQuarterNote & 0x0000ff00) >> 8);
	e.push_back((microsecsPerQuarterNote & 0x000000ff));

	return e;
}

void parseNextChunk(char* buffer, uint32_t& offset, uint32_t dt, uint8_t velocity, CxxMidi::File& myFile, std::string& songName, uint32_t microsecsPerQuarterNote, 
					std::string instruments[], int& currentProgramNumber, int& currentTrackNumber, std::vector<AMidiTrack>& lstTargetTracks, bool explodingTracks,
					std::string copyright)
{
	std::string str;
	str += buffer[offset];
	str += buffer[offset + 1];
	str += buffer[offset + 2];
	str += buffer[offset + 3];

	uint32_t length = computeLength((unsigned char*)buffer, offset + 4);

	if (str.compare("TRAK") == 0)
	{
		std::cout << "Found track with length = " << length << std::endl;
		currentTrackNumber++;

		CxxMidi::Track* pMidiTrack = NULL;

		if (!explodingTracks) {
			pMidiTrack = new CxxMidi::Track();
			std::string trackName = songName + "/" + std::to_string(currentTrackNumber);

			AMidiTrack targetTrack = AMidiTrack();
			targetTrack.trackName = trackName;

			if (!alreadyPresent<AMidiTrack>(lstTargetTracks, targetTrack)) {
				std::cout << "Adding track '" << trackName << "'" << std::endl;
				if (copyright.length() > 0) {
					pMidiTrack->push_back(setCopyright(copyright));
				}
				pMidiTrack->push_back(setTrackName(trackName));
				pMidiTrack->push_back(setTempo(microsecsPerQuarterNote));

				targetTrack.pTrack = pMidiTrack;
				targetTrack.trackNumber = currentTrackNumber;
				targetTrack.programNumber = -1;
				targetTrack.init = true;
				targetTrack.time = 0;

				createIndex<AMidiTrack>(lstTargetTracks, targetTrack);
			}
		}

		uint32_t time = 0;

		std::vector<MidiEvent> events;

		for (uint32_t pSEvent = offset + 8; pSEvent < offset + length; pSEvent += 2)
		{
			currentProgramNumber = -1;
			parseSEvent((unsigned char*)buffer, pSEvent, dt, time, velocity, events, currentProgramNumber);
			if (currentProgramNumber >= 0) {
				if (explodingTracks) {
					std::string trackName = songName + "/" + std::to_string(currentTrackNumber) + "/" + instruments[currentProgramNumber];

					CxxMidi::Track* pTrack = new CxxMidi::Track();

					AMidiTrack targetTrack = AMidiTrack();
					targetTrack.trackName = trackName;

					if (!alreadyPresent<AMidiTrack>(lstTargetTracks, targetTrack)) {
						std::cout << "Adding track '" << trackName << "'" << std::endl;

						if (copyright.length() > 0) {
							pTrack->push_back(setCopyright(copyright));
						}
						pTrack->push_back(setTrackName(trackName));
						pTrack->push_back(setTempo(microsecsPerQuarterNote));

						targetTrack.pTrack = pTrack;
						targetTrack.trackNumber = currentTrackNumber;
						targetTrack.programNumber = currentProgramNumber;
						targetTrack.init = false;
						targetTrack.time = 0;

						if (!targetTrack.init) {
							targetTrack.pTrack->push_back(CxxMidi::Event(0, // deltatime
								CxxMidi::Message::ProgramChange, // message type
								currentProgramNumber)); // program number

							targetTrack.init = true;
						}
						createIndex<AMidiTrack>(lstTargetTracks, targetTrack);
					}
				}
			}
		}

		std::stable_sort(events.begin(), events.end(), MidiEventCompare);

		AMidiTrack* pCurrentTrack = NULL;
		time = 0;

		for (std::vector<MidiEvent>::iterator it = events.begin(); it != events.end(); ++it)
		{
			MidiEvent& event = *it;
			if (event.progChange)
			{
				if (!explodingTracks) {
					pMidiTrack->push_back(CxxMidi::Event(0, // deltatime
						CxxMidi::Message::ProgramChange, // message type
						event.data)); // program number
				}
				else {
					pCurrentTrack = &lstTargetTracks.at(getMidiTrackFromProgram(lstTargetTracks, currentTrackNumber, event.data));

					if (!pCurrentTrack->init) {
						pCurrentTrack->pTrack->push_back(CxxMidi::Event(0, // deltatime
							CxxMidi::Message::ProgramChange, // message type
							event.data)); // program number

						pCurrentTrack->init = true;
					}
				}
			}
			else
			{
				if (event.velocity == 0)
				{
					//Note off
					if (event.tied)
					{
						//Search for contiguous note with the same pitch to tie with
						bool found = false;
						for (std::vector<MidiEvent>::iterator it2 = it + 1; it2 != events.end(); ++it2)
						{
							MidiEvent& event2 = *it2;
							if ((event2.velocity > 0) && (event2.data == event.data) && (event2.time == event.time))
							{
								//Contiguous note found, don't insert note off for current note and note on for next
								found = true;
								events.erase(it2);
								break;
							}
						}

						if (!found)
						{
							std::cout << "Cannnot find tied event. Ignoring tie." << std::endl;
							//Forcing Note off
							if (!explodingTracks) {
								pMidiTrack->push_back(CxxMidi::Event(event.time - time, // deltatime
									CxxMidi::Message::NoteOn, // message type
									event.data, // pitch
									0)); // velocity = 0 => NoteOff
							}
							else {
								pCurrentTrack->pTrack->push_back(CxxMidi::Event(event.time - pCurrentTrack->time, // deltatime
									CxxMidi::Message::NoteOn, // message type
									event.data, // pitch
									0)); // velocity = 0 => NoteOff
							}
						}
					}
					else
					{
						//Simple note off
						if (!explodingTracks) {
							pMidiTrack->push_back(CxxMidi::Event(event.time - time, // deltatime
								CxxMidi::Message::NoteOn, // message type
								event.data, // pitch
								0)); // velocity = 0 => NoteOff
						}
						else {
							pCurrentTrack->pTrack->push_back(CxxMidi::Event(event.time - pCurrentTrack->time, // deltatime
								CxxMidi::Message::NoteOn, // message type
								event.data, // pitch
								0)); // velocity = 0 => NoteOff
						}
					}
				}
				else
				{
					//NoteOn
					if (!explodingTracks) {
						pMidiTrack->push_back(CxxMidi::Event(event.time - time, // deltatime
							CxxMidi::Message::NoteOn, // message type
							event.data, // pitch
							event.velocity)); //velocity
					}
					else {
						pCurrentTrack->pTrack->push_back(CxxMidi::Event(event.time - pCurrentTrack->time, // deltatime
							CxxMidi::Message::NoteOn, // message type
							event.data, // pitch
							event.velocity)); //velocity
					}
				}

				if (!explodingTracks) {
					time = event.time;
				}
				else {
					pCurrentTrack->time = event.time;
				}
			}
		}

		if (!explodingTracks) {
			pMidiTrack->push_back(CxxMidi::Event(0, // deltatime
				CxxMidi::Message::Meta,
				CxxMidi::Message::EndOfTrack));
		}
		else {
			for (AMidiTrack aTrack : lstTargetTracks) {
				aTrack.pTrack->push_back(CxxMidi::Event(0, // deltatime
					CxxMidi::Message::Meta,
					CxxMidi::Message::EndOfTrack));
			}
		}
	}
	else if (str.compare("INS1") == 0)
	{
		//Instrument
		uint8_t registerNb = buffer[offset + 8];
		uint8_t type       = buffer[offset + 9];
		uint8_t data1      = buffer[offset + 10];
		uint8_t data2      = buffer[offset + 11];

		std::string name;
		for (uint32_t i = 12; i < length; ++i)
			name += buffer[offset + i];

		currentProgramNumber = (int)registerNb;
		std::cout << "Found instrument name = '" << name << "', register number = " << (int)registerNb << std::endl;

		instruments[(int)registerNb] = name;

		switch (type) {
			case 0:
				std::cout << "  Standard Chip Instrument" << std::endl;
				break;
			case 1:
			default:
				std::cout << "  MIDI Instrument, channel = " << (int)data1 << ", preset = " << (int)data2 << std::endl;
				break;
		}
	}
	else if (str.compare("NAME") == 0)
	{
		songName = "";
		for (uint32_t i = 8; i < length; ++i)
			songName += buffer[offset + i];
		std::cout << "Found song name = '" << songName << "'" << std::endl;
	}
	else
	{
		//(c) , AUTH , ANNO
		std::string text;
		for (uint32_t i = 8; i < length; ++i)
			text += buffer[offset + i];
		std::cout << "Found " << str << " chunk with length = " << length << " and text = " << text <<
			" -(for your information only , not using this chunk)." << std::endl;
	}

	offset += length;
	if (offset % 2) ++offset; //2-byte data alignment
}

int main(int argc, char** argv)
{
	bool explodingTracks = false;
	std::string copyright = "";

	if (argc == 1)
	{
		std::cout << "USAGE : " << argv[0] << " [-c\"CopyRight\"] [-explode] smus_file1[smus_file2[smus_file3[...]]]" << std::endl;
		exit(0);
	}

	for (int i = 1; i < argc; ++i)
	{
		std::cout << "============= Parsing " << argv[i] << " ==============" << std::endl;

		std::string s = std::string(argv[i]);
		if (s.at(0) == '-')
		{
			// Parameter
			if (s.length() < 2)
			{
				// Ignore it
			}
			else 
			{
				// Valid parameter
				switch (s.at(1))
				{
					case 'c':
					case 'C':
						copyright = s.substr(2);
						std::cout << "Copyright to apply : '" << copyright << "'" << std::endl;
						break;
					case 'e':
					case 'E':
						explodingTracks = true;
						std::cout << "Want to explode tracks (one track per instrument)" << std::endl;
						break;
					default:
						// Ignore it
						break;
				}
			}
		}
		else
		{
			CxxMidi::File myFile;

			std::ifstream smusFile(argv[i], std::ifstream::binary);

			if (smusFile) {
				// get length of file:
				smusFile.seekg(0, smusFile.end);
				int length = smusFile.tellg();
				smusFile.seekg(0, smusFile.beg);

				char* buffer = new char[length];

				std::cout << "Reading " << length << " characters... " << std::endl;
				// read data as a block:
				smusFile.read(buffer, length);

				smusFile.close();

				// ...buffer contains the entire file...

				std::string form;
				form += buffer[0];
				form += buffer[0 + 1];
				form += buffer[0 + 2];
				form += buffer[0 + 3];
				if (form.compare("FORM"))
				{
					std::cerr << "Cant find FORM chunk. Exiting." << std::endl;
				}
				else
				{
					std::cout << "FORM chunk found. File is effectively an IFF file." << std::endl;
					uint32_t formLength = computeLength((unsigned char*)buffer, 4);
					std::cout << "FORM chunk length = " << formLength << std::endl;
					//Looking for SMUS tag
					std::string smus;
					smus += buffer[8];
					smus += buffer[9];
					smus += buffer[10];
					smus += buffer[11];
					if (smus.compare("SMUS"))
					{
						std::cerr << "Cant find SMUS. Exiting." << std::endl;
					}
					else
					{
						std::cout << "SMUS found. File is effectively a SMUS file." << std::endl;
						//Looking for SHDR tag
						std::string shdr;
						shdr += buffer[12];
						shdr += buffer[13];
						shdr += buffer[14];
						shdr += buffer[15];
						if (shdr.compare("SHDR"))
						{
							std::cerr << "Cant find SHDR. Exiting." << std::endl;
						}
						else
						{
							//Parsing SHDR
							std::cout << "SHDR chunk found." << std::endl;
							uint32_t shdrLength = computeLength((unsigned char*)buffer, 16);
							std::cout << "SHDR chunk length = " << shdrLength << std::endl;
							unsigned char* unsignedBuffer = (unsigned char*)buffer;
							uint16_t tempo = ((unsignedBuffer[20] << 8) + unsignedBuffer[21]) >> 7;

							uint32_t dt; // quartenote deltatime [ticks]
							// What value should dt be, if we want quarter notes to last 0.5s?

							// Default MIDI time division is 500ticks/quarternote.
							// Default MIDI tempo is 500000us per quarternote
							dt = CxxMidi::Converters::us2dt(500000, // 0.5s
								500000, // tempo [us/quarternote]
								500); // time division [ticks/quarternote]

							uint8_t velocity = unsignedBuffer[22];
							std::cout << "Found Volume = " << (int)velocity << std::endl;
							uint8_t tracks = unsignedBuffer[23];
							std::cout << "Found track number = " << (int)tracks << std::endl;
							std::cout << "End of fixed headers. Now Parsing variable chunks." << std::endl;

							//Parsing variable chunks
							uint32_t offset = 24;

							std::string songName = "";
							uint32_t microsecsPerQuarterNote = 500000 * 120 / tempo;

							std::vector<AMidiTrack> lstTargetTracks = std::vector<AMidiTrack>();
							int currentProgramNumber;
							int currentTrackNumber = 0;
							std::string instruments[256];
							int instruments_size = sizeof(instruments) / sizeof(instruments[0]);
							for (int i = 0; i < instruments_size; i++) {
								instruments[i] = "";
							}

							while (offset < formLength) {
								currentProgramNumber = 0;
								parseNextChunk(buffer, offset, dt, velocity, myFile, songName, microsecsPerQuarterNote,
									instruments, currentProgramNumber, currentTrackNumber, lstTargetTracks, explodingTracks, copyright);
							}

							for (int i = 0; i < instruments_size; i++) {
								if (instruments[i] != "") {
									std::cout << "Instrument " << i << " : '" << instruments[i] << "'" << std::endl;
								}
							}

							int numTrack = 1;
							for (AMidiTrack track : lstTargetTracks) {
								std::cout << "Track " << numTrack++ << " : '" << track.trackName << "'" << std::endl;
								myFile.push_back(*track.pTrack);
							}

							std::string newTitle(argv[i]);
							std::string newExtension(".mid");
							size_t dotIndex = newTitle.find_last_of('.');
							newTitle.replace(dotIndex, newTitle.length() - dotIndex, newExtension);
							myFile.saveAs(newTitle.c_str());
							std::cout << "============= " << newTitle << " saved! ==============" << std::endl;

							numTrack = 1;
							for (AMidiTrack track : lstTargetTracks) {
								std::cout << "deleteTrack " << numTrack++ << " : '" << track.trackName << "'" << std::endl;
								delete(track.pTrack);
							}

						}
					}
				}


				delete[] buffer;
			}
			else
				std::cerr << "Cant open file " << argv[i] << std::endl;
		}
	}
	return 0;
}