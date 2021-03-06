#pragma once
#include "stdafx.h"
#include "BaseMapper.h"

class Mapper15 : public BaseMapper
{
protected:
	virtual uint16_t GetPRGPageSize() { return 0x2000; }
	virtual uint16_t GetCHRPageSize() { return 0x2000; }

	void InitMapper()
	{
		WriteRegister(0x8000, 0);
		SelectCHRPage(0, 0);
	}

	void WriteRegister(uint16_t addr, uint8_t value)
	{
		SetMirroringType(value & 0x40 ? MirroringType::Horizontal : MirroringType::Vertical);
		
		uint8_t subBank = value >> 7;
		uint8_t bank = (value & 0x7F) << 1;
		switch(addr & 0x03) {
			case 0:
				SelectPRGPage(0, bank  ^ subBank);
				SelectPRGPage(1, (bank + 1) ^ subBank);
				SelectPRGPage(2, (bank + 2) ^ subBank);
				SelectPRGPage(3, (bank + 3) ^ subBank);
				break;

			case 1:
			case 3:
				bank |= subBank;
				SelectPRGPage(0, bank);
				SelectPRGPage(1, bank + 1);
				SelectPRGPage(2, bank + ((addr & 0x02) ? 0 : 1));
				SelectPRGPage(3, bank + 1);
				break;

			case 2:
				bank |= subBank;
				SelectPRGPage(0, bank);
				SelectPRGPage(1, bank);
				SelectPRGPage(2, bank);
				SelectPRGPage(3, bank);
				break;
		}
	}
};
