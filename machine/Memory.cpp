//
// Created by ross.bamford on 01/04/2019.
//

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include "Memory.h"

namespace rosco {
    namespace m68k {
        namespace emu {
            Memory::Memory(const uint32_t size) {
                this->size = size;
                this->store = new uint8_t[size]();
            }

            Memory::~Memory() {
                delete[] this->store;
            }

            uint32_t Memory::read32(const uint32_t address) {
#ifdef MEM_TRACE
                uint32_t *addr = ((uint32_t*)(this->store + address));
                std::cout << "read32 : " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                // Big-endian; the halves of an access that straddles the end
                // are read as such (0xFF past the end, like read8)
                if (address < this->size && this->size - address >= 4) {
                    const uint8_t *p = this->store + address;
                    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
                } else {
                    return (uint32_t)read16(address) << 16 | read16(address + 2);
                }
            }

            uint16_t Memory::read16(const uint32_t address) {
#ifdef MEM_TRACE
                uint16_t *addr = ((uint16_t*)(this->store + address));
                std::cout << "read16 : " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                if (address < this->size && this->size - address >= 2) {
                    const uint8_t *p = this->store + address;
                    return (uint16_t)(p[0] << 8 | p[1]);
                } else {
                    return (uint16_t)(read8(address) << 8 | read8(address + 1));
                }
            }

            uint8_t Memory::read8(const uint32_t address) {
#ifdef MEM_TRACE
                uint8_t *addr = ((uint8_t*)(this->store + address));
                std::cout << "read8 : " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                if (address < this->size) {
                    return *(this->store + address);
                } else {
                    return 0xFF;
                }
            }

            void Memory::write32(const uint32_t address, uint32_t data) {
#ifdef MEM_TRACE
                uint32_t *addr = ((uint32_t*)(this->store + address));
                std::cout << "write32: " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                write16(address, data >> 16);
                write16(address + 2, data & 0xFFFF);
            }

            void Memory::write16(const uint32_t address, uint16_t data) {
#ifdef MEM_TRACE
                uint16_t *addr = ((uint16_t*)(this->store + address));
                std::cout << "write16: " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                write8(address, data >> 8);
                write8(address + 1, data & 0xFF);
            }

            void Memory::write8(const uint32_t address, uint8_t data) {
#ifdef MEM_TRACE
                uint8_t *addr = this->store + address;
                std::cout << "write8: " << std::hex << addr << "; Base: " << (uint32_t*)this->store << "; relative: " << address << std::endl;
#endif

                if (address < this->size) {
                    *(this->store + address) = data;
                }
            }

            void Memory::LoadData(std::uint32_t baseAddr, char const* filename) {
                std::ifstream ifs(filename, std::ios::binary | std::ios::ate);

                if (!ifs) {
                    throw std::runtime_error(std::string("cannot open ") + filename);
                }

                std::ifstream::pos_type pos = ifs.tellg();

                if (baseAddr > this->size || pos > this->size - baseAddr) {
                    throw std::runtime_error(std::string(filename) + " does not fit in memory");
                } else {
                    ifs.seekg(0, std::ios::beg);
                    ifs.read((char*)&this->store[baseAddr], pos);
                }

            }

        }
    }
}
