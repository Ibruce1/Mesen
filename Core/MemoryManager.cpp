#include "stdafx.h"
#include "MemoryManager.h"
#include "BaseMapper.h"
#include "Debugger.h"
#include "CheatManager.h"

//Used for open bus
uint8_t MemoryManager::_lastReadValue = 0;

MemoryManager::MemoryManager(shared_ptr<BaseMapper> mapper)
{
	_mapper = mapper;
	_lastReadValue = 0;

	_internalRAM = new uint8_t[InternalRAMSize];
	for(int i = 0; i < 2; i++) {
		_nametableRAM[i] = new uint8_t[NameTableScreenSize];
		_mapper->InitializeRam(_nametableRAM[i], NameTableScreenSize);
	}

	_mapper->SetDefaultNametables(_nametableRAM[0], _nametableRAM[1]);

	_ramReadHandlers = new IMemoryHandler*[RAMSize];
	_ramWriteHandlers = new IMemoryHandler*[RAMSize];

	memset(_ramReadHandlers, 0, RAMSize * sizeof(IMemoryHandler*));
	memset(_ramWriteHandlers, 0, RAMSize * sizeof(IMemoryHandler*));
}

MemoryManager::~MemoryManager()
{
	delete[] _internalRAM;
	for(int i = 0; i < 2; i++) {
		delete[] _nametableRAM[i];
	}

	delete[] _ramReadHandlers;
	delete[] _ramWriteHandlers;
}

void MemoryManager::Reset(bool softReset)
{
	if(!softReset) {
		_mapper->InitializeRam(_internalRAM, InternalRAMSize);
	}

	_mapper->Reset(softReset);
}

uint8_t MemoryManager::ReadRegister(uint16_t addr)
{
	if(_ramReadHandlers[addr]) {
		return _ramReadHandlers[addr]->ReadRAM(addr);
	} else {
		return GetOpenBus();
	}
}

void MemoryManager::WriteRegister(uint16_t addr, uint8_t value)
{
	if(_ramWriteHandlers[addr]) {
		_ramWriteHandlers[addr]->WriteRAM(addr, value);
	}
}

void MemoryManager::InitializeMemoryHandlers(IMemoryHandler** memoryHandlers, IMemoryHandler* handler, vector<uint16_t> *addresses, bool allowOverride)
{
	for(uint16_t address : *addresses) {
		if(!allowOverride && memoryHandlers[address] != nullptr) {
			throw std::runtime_error("Not supported");
		}
		memoryHandlers[address] = handler;
	}
}

void MemoryManager::RegisterIODevice(IMemoryHandler *handler)
{
	MemoryRanges ranges;
	handler->GetMemoryRanges(ranges);

	InitializeMemoryHandlers(_ramReadHandlers, handler, ranges.GetRAMReadAddresses(), ranges.GetAllowOverride());
	InitializeMemoryHandlers(_ramWriteHandlers, handler, ranges.GetRAMWriteAddresses(), ranges.GetAllowOverride());
}

uint8_t* MemoryManager::GetInternalRAM()
{
	return _internalRAM;
}

uint8_t MemoryManager::DebugRead(uint16_t addr)
{
	uint8_t value = 0x00;
	if(addr <= 0x1FFF) {
		value = _internalRAM[addr & 0x07FF];
	} else if(addr > 0x4017) {
		value = ReadRegister(addr);
	}

	CheatManager::ApplyRamCodes(addr, value);

	return value;
}

void MemoryManager::ProcessCpuClock()
{
	_mapper->ProcessCpuClock();
}

uint8_t MemoryManager::Read(uint16_t addr, MemoryOperationType operationType)
{
	uint8_t value;
	if(addr <= 0x1FFF) {
		value = _internalRAM[addr & 0x07FF];
	} else {
		value = ReadRegister(addr);
	}

	CheatManager::ApplyRamCodes(addr, value);

	Debugger::ProcessRamOperation(operationType, addr, value);

	_lastReadValue = value;

	return value;
}

void MemoryManager::Write(uint16_t addr, uint8_t value)
{
	Debugger::ProcessRamOperation(MemoryOperationType::Write, addr, value);

	if(addr <= 0x1FFF) {
		_internalRAM[addr & 0x07FF] = value;
	} else {
		WriteRegister(addr, value);
	}
}

void MemoryManager::DebugWrite(uint16_t addr, uint8_t value)
{
	if(addr <= 0x1FFF) {
		_internalRAM[addr & 0x07FF] = value;
	} else {
		WriteRegister(addr, value);
	}
}

void MemoryManager::ProcessVramAccess(uint16_t &addr)
{
	addr &= 0x3FFF;
	if(addr >= 0x3000) {
		//Need to mirror 0x3000 writes to 0x2000, this appears to be how hardware behaves
		//Required for proper MMC3 IRQ timing in Burai Fighter
		addr -= 0x1000;
	}
	_mapper->NotifyVRAMAddressChange(addr);
}

uint8_t MemoryManager::DebugReadVRAM(uint16_t addr)
{
	addr &= 0x3FFF;
	if(addr >= 0x3000) {
		addr -= 0x1000;
	}
	return _mapper->ReadVRAM(addr, MemoryOperationType::Read);
}

uint8_t MemoryManager::ReadVRAM(uint16_t addr, MemoryOperationType operationType)
{	
	ProcessVramAccess(addr);
	uint8_t value = _mapper->ReadVRAM(addr, operationType);
	Debugger::ProcessVramOperation(operationType, addr, value);
	return value;
}

void MemoryManager::WriteVRAM(uint16_t addr, uint8_t value)
{
	Debugger::ProcessVramOperation(MemoryOperationType::Write, addr, value);	
	ProcessVramAccess(addr);
	_mapper->WriteVRAM(addr, value);
}

uint32_t MemoryManager::ToAbsoluteChrAddress(uint16_t vramAddr)
{
	return _mapper->ToAbsoluteChrAddress(vramAddr);
}

void MemoryManager::StreamState(bool saving)
{
	ArrayInfo<uint8_t> internalRam = { _internalRAM, MemoryManager::InternalRAMSize };
	ArrayInfo<uint8_t> nameTable0Ram = { _nametableRAM[0], MemoryManager::NameTableScreenSize };
	ArrayInfo<uint8_t> nameTable1Ram = { _nametableRAM[1], MemoryManager::NameTableScreenSize };
	Stream(internalRam, nameTable0Ram, nameTable1Ram);
}

uint8_t MemoryManager::GetOpenBus(uint8_t mask)
{
	return _lastReadValue & mask;
}