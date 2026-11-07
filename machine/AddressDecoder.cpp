//
// Created by ross.bamford on 22/04/2019.
//

#include <iostream>
#include "AddressDecoder.h"

namespace rosco {
    namespace m68k {
        namespace emu {
            AddressDecoder::AddressDecoder(std::uint32_t romsize, std::uint32_t ramsize, char const* filename) {
                this->rom = std::unique_ptr<Memory>(new Memory(romsize));
                this->ram = std::unique_ptr<Memory>(new Memory(ramsize));
                this->bootLineActive = true;
                this->bootReadCount = 0;

#ifdef MEM_TRACE
                std::cout << "Initialized with " << this->ram->size << " bytes RAM and " << this->rom->size << " bytes ROM" << std::endl;
#endif

                ReadRomData(filename);
            }

            Memory* AddressDecoder::getMemoryForAddress(std::uint32_t address) {
                if (address < this->ram->size) {
#ifdef MEM_TRACE
                    std::cout << "Read RAM @ " << std::hex << address << std::endl;
#endif
                    return this->ram.get();
                } else if (address >= 0x00e00000 && address < 0x00f00000) {
#ifdef MEM_TRACE
                    std::cout << "Read ROM @ " << std::hex << address << std::endl;
#endif
                    return this->rom.get();
                } else {
                    // /BERR?
#ifdef MEM_TRACE
                    std::cout << "BusError @ " << std::hex << address << std::endl;
#endif
                    return NULL;
                }
            }

            Memory* AddressDecoder::getWritableMemoryForAddress(std::uint32_t address) {
                Memory *mem = this->getMemoryForAddress(address);

                if (mem == this->rom.get() && !this->romWritable) {
                    return NULL;    // flash without write enable: ignored
                }
                return mem;
            }

            bool AddressDecoder::isMapped(std::uint32_t address, std::uint32_t size) {
                for (std::uint32_t a : {address, address + size - 1}) {
                    bool duart = this->duart != nullptr && Duart::contains(a);
                    if (!duart && this->getMemoryForAddress(a) == NULL) {
                        return false;
                    }
                }
                return true;
            }

            std::uint32_t AddressDecoder::makeRelativeAddress(std::uint32_t address) {
                if (address < this->ram->size) {
#ifdef MEM_TRACE
                    std::cout << "RAM Relative address is: 0x" << std::hex << address << std::endl;
#endif
                    return address;
                } else if (address >= 0x00e00000 && address < 0x00f00000) {
#ifdef MEM_TRACE
                    std::cout << "ROM Relative address is: 0x" << std::hex << address - (0x00e00000) << std::endl;
#endif
                    return address - (0x00e00000);
                } else {
                    // /BERR?
#ifdef MEM_TRACE
                    std::cout << "BUSERROR Relative address is 0x0" << std::endl;
#endif
                    return 0;
                }
            }

            void AddressDecoder::reset() {
                this->bootLineActive = true;
                this->bootReadCount = 0;
            }

            std::uint32_t AddressDecoder::read32(std::uint32_t address) {
                Memory *mem;

                if (this->duart != nullptr && Duart::contains(address)) {
                    return (std::uint32_t)this->read16(address) << 16 | this->read16(address + 2);
                }

                if (this->bootLineActive && address < this->rom->size) {
                    if (this->bootReadCount++ < 1) {
#ifdef MEM_TRACE
                        std::cout << "Read /BOOT-shadowed ROM @ " << std::hex << address << std::endl;
#endif
                        mem = this->rom.get();
                    } else {
#ifdef MEM_TRACE
                        std::cout << "Deassert /BOOT and read ROM @ " << std::hex << address << std::endl;
#endif
                        this->bootLineActive = false;
                        mem = this->rom.get();
                    }
                } else {
                    mem = this->getMemoryForAddress(address);
                }

                if (mem != NULL) {
                    uint32_t dw = mem->read32(makeRelativeAddress(address));
                    return dw;
                } else {
                    // /BERR!
                    return 0;
                }
            }

            std::uint16_t AddressDecoder::read16(std::uint32_t address) {
                if (this->duart != nullptr && Duart::contains(address)) {
                    // 8-bit device: the registers are the odd bytes
                    return (std::uint16_t)(this->duart->read(address) << 8 | this->duart->read(address + 1));
                }

                Memory *mem = this->getMemoryForAddress(address);

                if (mem != NULL) {
                    return mem->read16(makeRelativeAddress(address));
                } else {
                    // /BERR!
                    return 0;
                }
            }

            std::uint8_t AddressDecoder::read8(std::uint32_t address) {
                if (this->duart != nullptr && Duart::contains(address)) {
                    return this->duart->read(address);
                }

                Memory *mem = this->getMemoryForAddress(address);

                if (mem != NULL) {
                    return mem->read8(makeRelativeAddress(address));
                } else {
                    // /BERR!
                    return 0;
                }
            }

            void AddressDecoder::write32(std::uint32_t address, std::uint32_t data) {
                if (this->duart != nullptr && Duart::contains(address)) {
                    this->write16(address, data >> 16);
                    this->write16(address + 2, data & 0xFFFF);
                    return;
                }

                Memory *mem = this->getWritableMemoryForAddress(address);

                if (mem != NULL) {
                    mem->write32(makeRelativeAddress(address), data);
                } else {
                    // /BERR!
                }
            }

            void AddressDecoder::write16(std::uint32_t address, std::uint16_t data) {
                if (this->duart != nullptr && Duart::contains(address)) {
                    this->duart->write(address, data >> 8);
                    this->duart->write(address + 1, data & 0xFF);
                    return;
                }

                Memory *mem = this->getWritableMemoryForAddress(address);

                if (mem != NULL) {
                    mem->write16(makeRelativeAddress(address), data);
                } else {
                    // /BERR!
                }
            }

            void AddressDecoder::write8(std::uint32_t address, std::uint8_t data) {
                if (this->duart != nullptr && Duart::contains(address)) {
                    this->duart->write(address, data);
                    return;
                }

                Memory *mem = this->getWritableMemoryForAddress(address);

                if (mem != NULL) {
                    mem->write8(makeRelativeAddress(address), data);
                } else {
                    // /BERR!
                }
            }

            void AddressDecoder::ReadRomData(char const* filename) {
                this->rom->LoadData(0, filename);
            }

            void AddressDecoder::LoadMemoryFile(const uint32_t baseAddr, char const* filename) {
                this->ram->LoadData(baseAddr, filename);
            }
        }
    }
};
