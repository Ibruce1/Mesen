#include "stdafx.h"
#include "MessageManager.h"
#include "Movie.h"
#include "Console.h"
#include "../Utilities/FolderUtilities.h"
#include "RomLoader.h"
#include "CheatManager.h"
#include "SaveStateManager.h"

shared_ptr<Movie> Movie::_instance(new Movie());

Movie::~Movie()
{
	_instance = nullptr;
}

shared_ptr<Movie> Movie::GetInstance()
{
	return _instance;
}

void Movie::PushState(uint8_t port)
{
	if(_counter[port] > 0) {
		uint16_t data = _lastState[port] << 8 | _counter[port];
		_data.PortData[port].push_back(data);

		_lastState[port] = 0;
		_counter[port] = 0;
	}
}

void Movie::RecordState(uint8_t port, uint8_t state)
{
	if(_recording) {
		if(_lastState[port] != state || _counter[port] == 0) {
			if(_counter[port] != 0) {
				PushState(port);
			}
			_lastState[port] = state;
			_counter[port] = 1;
		} else {
			_counter[port]++;

			if(_counter[port] == 255) {
				PushState(port);
			}
		}
	}
}

uint8_t Movie::GetState(uint8_t port)
{
	uint16_t data = --_data.PortData[port][_readPosition[port]];
	if((data & 0xFF) == 0) {
		_readPosition[port]++;
	}

	if(_readPosition[port] >= _data.DataSize[port]) {
		//End of movie file
		MessageManager::DisplayMessage("Movies", "MovieEnded");
		MessageManager::SendNotification(ConsoleNotificationType::MovieEnded);
		if(EmulationSettings::CheckFlag(EmulationFlags::PauseOnMovieEnd)) {
			EmulationSettings::SetFlags(EmulationFlags::Paused);
		}
		_playing = false;
	}

	return (data >> 8);
}

void Movie::Reset()
{
	_startState.clear();
	_startState.seekg(0, ios::beg);
	_startState.seekp(0, ios::beg);

	memset(_readPosition, 0, 4 * sizeof(uint32_t));
	memset(_counter, 0, 4);
	memset(_lastState, 0, 4);
	_data = MovieData();

	_recording = false;
	_playing = false;
}

void Movie::StartRecording(string filename, bool reset)
{
	_filename = filename;
	_file.open(filename, ios::out | ios::binary);

	if(_file) {
		Console::Pause();

		Reset();

		if(reset) {
			//Movies need a fixed power up state to be identical on each replay, force all 0s for RAM, no matter the setting
			RamPowerOnState originalState = EmulationSettings::GetRamPowerOnState();
			EmulationSettings::SetRamPowerOnState(RamPowerOnState::AllZeros);
			Console::Reset(false);
			EmulationSettings::SetRamPowerOnState(originalState);
		} else {
			Console::SaveState(_startState);
		}

		_recording = true;

		Console::Resume();

		MessageManager::DisplayMessage("Movies", "MovieRecordingTo", FolderUtilities::GetFilename(filename, true));
	}
}

void Movie::StopAll()
{
	if(_recording) {
		_recording = false;
		for(int i = 0; i < 4; i++) {
			PushState(i);
		}
		Save();
	}
	if(_playing) {
		MessageManager::DisplayMessage("Movies", "MovieStopped");
		_playing = false;
	}
}

void Movie::PlayMovie(stringstream &filestream, bool autoLoadRom, string filename)
{
	StopAll();

	Reset();

	Console::Pause();
	if(Load(filestream, autoLoadRom)) {
		if(_startState.tellp() > 0) {
			//Restore state if one was present in the movie
			Console::LoadState(_startState);
		}

		CheatManager::SetCheats(_cheatList);
		_playing = true;

		if(!filename.empty()) {
			MessageManager::DisplayMessage("Movies", "MoviePlaying", FolderUtilities::GetFilename(filename, true));
		}
	}
	Console::Resume();
}

void Movie::Record(string filename, bool reset)
{
	if(_instance) {
		_instance->StartRecording(filename, reset);
	}
}

void Movie::Play(string filename)
{
	if(_instance) {
		ifstream file(filename, ios::in | ios::binary);
		std::stringstream ss;

		if(file) {
			ss << file.rdbuf();
			file.close();

			_instance->PlayMovie(ss, true, filename);
		}
	}
}

void Movie::Play(std::stringstream &filestream, bool autoLoadRom)
{
	if(_instance) {
		_instance->PlayMovie(filestream, autoLoadRom);
	}
}

void Movie::Stop()
{
	if(_instance) {
		_instance->StopAll();
	}
}

bool Movie::Playing()
{
	if(_instance) {
		return _instance->_playing;
	} else {
		return false;
	}
}

bool Movie::Recording()
{
	if(_instance) {
		return _instance->_recording;
	} else {
		return false;
	}
}

struct MovieHeader
{
	char Header[3] = { 'M', 'M', 'O' };
	uint32_t MesenVersion;
	uint32_t MovieFormatVersion;
	uint32_t SaveStateFormatVersion;
	uint32_t RomCrc32;
	uint32_t Region;
	uint32_t ConsoleType;
	uint8_t ControllerTypes[4];
	uint32_t ExpansionDevice;
	uint32_t OverclockRate;
	bool OverclockAdjustApu;
	uint32_t ExtraScanlinesBeforeNmi;
	uint32_t ExtraScanlinesAfterNmi;
	uint32_t CheatCount;
	uint32_t FilenameLength;
};

bool Movie::Save()
{
	string romFilename = Console::GetRomName();

	MovieHeader header = {};
	header.MesenVersion = EmulationSettings::GetMesenVersion();
	header.MovieFormatVersion = Movie::MovieFormatVersion;
	header.SaveStateFormatVersion = SaveStateManager::FileFormatVersion;
	header.RomCrc32 = Console::GetCrc32();
	header.Region = (uint32_t)Console::GetNesModel();
	header.ConsoleType = (uint32_t)EmulationSettings::GetConsoleType();
	header.ExpansionDevice = (uint32_t)EmulationSettings::GetExpansionDevice();
	header.OverclockRate = (uint32_t)EmulationSettings::GetOverclockRate();
	header.OverclockAdjustApu = EmulationSettings::GetOverclockAdjustApu();
	for(int port = 0; port < 4; port++) {
		header.ControllerTypes[port] = (uint32_t)EmulationSettings::GetControllerType(port);
	}
	header.FilenameLength = (uint32_t)romFilename.size();
	
	vector<CodeInfo> cheatList = CheatManager::GetCheats();
	header.CheatCount = (uint32_t)cheatList.size();

	_file.write((char*)header.Header, sizeof(header.Header));
	_file.write((char*)&header.MesenVersion, sizeof(header.MesenVersion));
	_file.write((char*)&header.MovieFormatVersion, sizeof(header.MovieFormatVersion));
	_file.write((char*)&header.SaveStateFormatVersion, sizeof(header.SaveStateFormatVersion));
	_file.write((char*)&header.RomCrc32, sizeof(header.RomCrc32));
	_file.write((char*)&header.Region, sizeof(header.Region));
	_file.write((char*)&header.ConsoleType, sizeof(header.ConsoleType));
	_file.write((char*)&header.ControllerTypes, sizeof(header.ControllerTypes));
	_file.write((char*)&header.ExpansionDevice, sizeof(header.ExpansionDevice));
	_file.write((char*)&header.OverclockRate, sizeof(header.OverclockRate));
	_file.write((char*)&header.OverclockAdjustApu, sizeof(header.OverclockAdjustApu));
	_file.write((char*)&header.ExtraScanlinesBeforeNmi, sizeof(header.ExtraScanlinesBeforeNmi));
	_file.write((char*)&header.ExtraScanlinesAfterNmi, sizeof(header.ExtraScanlinesAfterNmi));
	_file.write((char*)&header.CheatCount, sizeof(header.CheatCount));
	_file.write((char*)&header.FilenameLength, sizeof(header.FilenameLength));

	_file.write((char*)romFilename.c_str(), header.FilenameLength);

	for(CodeInfo cheatCode : cheatList) {
		_file.write((char*)&cheatCode.Address, sizeof(cheatCode.Address));
		_file.write((char*)&cheatCode.Value, sizeof(cheatCode.Value));
		_file.write((char*)&cheatCode.CompareValue, sizeof(cheatCode.CompareValue));
		_file.write((char*)&cheatCode.IsRelativeAddress, sizeof(cheatCode.IsRelativeAddress));
	}

	_data.SaveStateSize = (uint32_t)_startState.tellp();
	_file.write((char*)&_data.SaveStateSize, sizeof(uint32_t));
		
	if(_data.SaveStateSize > 0) {
		_startState.seekg(0, ios::beg);
		uint8_t *stateBuffer = new uint8_t[_data.SaveStateSize];
		_startState.read((char*)stateBuffer, _data.SaveStateSize);
		_file.write((char*)stateBuffer, _data.SaveStateSize);
		delete[] stateBuffer;
	}

	for(int i = 0; i < 4; i++) {
		_data.DataSize[i] = (uint32_t)_data.PortData[i].size();
		_file.write((char*)&_data.DataSize[i], sizeof(uint32_t));
		if(_data.DataSize[i] > 0) {
			_file.write((char*)&_data.PortData[i][0], _data.DataSize[i] * sizeof(uint16_t));
		}
	}

	_file.close();

	MessageManager::DisplayMessage("Movies", "MovieSaved", FolderUtilities::GetFilename(_filename, true));

	return true;
}

bool Movie::Load(std::stringstream &file, bool autoLoadRom)
{
	MovieHeader header = {};
	file.read((char*)header.Header, sizeof(header.Header));

	if(memcmp(header.Header, "MMO", 3) != 0) {
		//Invalid movie file
		MessageManager::DisplayMessage("Movies", "MovieInvalid");
		return false;
	}

	file.read((char*)&header.MesenVersion, sizeof(header.MesenVersion));

	if(header.MesenVersion > EmulationSettings::GetMesenVersion()) {
		MessageManager::DisplayMessage("Movies", "MovieNewerVersion");
		return false;
	}

	file.read((char*)&header.MovieFormatVersion, sizeof(header.MovieFormatVersion));
	if(header.MovieFormatVersion < 2 || header.MovieFormatVersion > Movie::MovieFormatVersion) {
		//Currently compatible with version 2 & 3
		MessageManager::DisplayMessage("Movies", "MovieIncompatibleVersion");
		return false;
	}

	file.read((char*)&header.SaveStateFormatVersion, sizeof(header.SaveStateFormatVersion));
	file.read((char*)&header.RomCrc32, sizeof(header.RomCrc32));
	file.read((char*)&header.Region, sizeof(header.Region));
	file.read((char*)&header.ConsoleType, sizeof(header.ConsoleType));
	file.read((char*)&header.ControllerTypes, sizeof(header.ControllerTypes));
	file.read((char*)&header.ExpansionDevice, sizeof(header.ExpansionDevice));
	if(header.MovieFormatVersion >= 3) {
		//New fields in version 3
		file.read((char*)&header.OverclockRate, sizeof(header.OverclockRate));
		file.read((char*)&header.OverclockAdjustApu, sizeof(header.OverclockAdjustApu));
		EmulationSettings::SetOverclockRate(header.OverclockRate, header.OverclockAdjustApu);

		if(header.MovieFormatVersion >= 4) {
			file.read((char*)&header.ExtraScanlinesBeforeNmi, sizeof(header.ExtraScanlinesBeforeNmi));
			file.read((char*)&header.ExtraScanlinesAfterNmi, sizeof(header.ExtraScanlinesAfterNmi));
			EmulationSettings::SetPpuNmiConfig(header.ExtraScanlinesBeforeNmi, header.ExtraScanlinesAfterNmi);
		}
	}
	file.read((char*)&header.CheatCount, sizeof(header.CheatCount));
	file.read((char*)&header.FilenameLength, sizeof(header.FilenameLength));

	EmulationSettings::SetConsoleType((ConsoleType)header.ConsoleType);
	EmulationSettings::SetExpansionDevice((ExpansionPortDevice)header.ExpansionDevice);
	for(int port = 0; port < 4; port++) {
		EmulationSettings::SetControllerType(port, (ControllerType)header.ControllerTypes[port]);
	}

	char* romFilename = new char[header.FilenameLength + 1];
	memset(romFilename, 0, header.FilenameLength + 1);
	file.read((char*)romFilename, header.FilenameLength);
	
	_cheatList.clear();
	CodeInfo cheatCode;
	for(uint32_t i = 0; i < header.CheatCount; i++) {
		file.read((char*)&cheatCode.Address, sizeof(cheatCode.Address));
		file.read((char*)&cheatCode.Value, sizeof(cheatCode.Value));
		file.read((char*)&cheatCode.CompareValue, sizeof(cheatCode.CompareValue));
		file.read((char*)&cheatCode.IsRelativeAddress, sizeof(cheatCode.IsRelativeAddress));
		_cheatList.push_back(cheatCode);
	}

	file.read((char*)&_data.SaveStateSize, sizeof(uint32_t));

	if(_data.SaveStateSize > 0) {
		if(header.SaveStateFormatVersion != SaveStateManager::FileFormatVersion) {
			MessageManager::DisplayMessage("Movies", "MovieIncompatibleVersion");
			return false;
		}
	}

	bool loadedGame = true;
	if(autoLoadRom) {
		string currentRom = Console::GetRomName();
		if(currentRom.empty() || header.RomCrc32 != Console::GetCrc32()) {
			//Loaded game isn't the same as the game used for the movie, attempt to load the correct game
			loadedGame = Console::LoadROM(romFilename, header.RomCrc32);
		} else {
			Console::Reset(false);
		}
	}

	if(loadedGame) {
		if(_data.SaveStateSize > 0) {
			uint8_t *stateBuffer = new uint8_t[_data.SaveStateSize];
			file.read((char*)stateBuffer, _data.SaveStateSize);
			_startState.write((char*)stateBuffer, _data.SaveStateSize);
			delete[] stateBuffer;
		}

		for(int i = 0; i < 4; i++) {
			file.read((char*)&_data.DataSize[i], sizeof(uint32_t));

			uint16_t* readBuffer = new uint16_t[_data.DataSize[i]];
			file.read((char*)readBuffer, _data.DataSize[i] * sizeof(uint16_t));
			_data.PortData[i] = vector<uint16_t>(readBuffer, readBuffer + _data.DataSize[i]);
			delete[] readBuffer;
		}
	} else {
		MessageManager::DisplayMessage("Movies", "MovieMissingRom", romFilename);
	}
	return loadedGame;
}