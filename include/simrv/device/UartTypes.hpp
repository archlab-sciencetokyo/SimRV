#pragma once

#include <cstdint>

// UART 16550A domain types
enum class UartRegister : uint8_t {
    RbrThrDll = 0,
    IerDlm = 1,
    IirFcr = 2,
    Lcr = 3,
    Mcr = 4,
    Lsr = 5,
    Msr = 6,
    Scr = 7
};
enum class UartRegStride : int8_t { Auto = -1, Byte = 0, Word = 2 };
