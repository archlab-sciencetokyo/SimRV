/**
 * @file StateControl.hpp
 * @brief SimRV state control helper interfaces.
 */
#pragma once

#include "Define.hpp"

class CPU;

class TlbUnit {
   public:
    explicit TlbUnit(CPU& cpu) : cpu_(cpu) {}

    void flush();

   private:
    CPU& cpu_;
};

class InterruptController {
   public:
    explicit InterruptController(CPU& cpu) : cpu_(cpu) {}

    void updateMip();
    void setIrq(int irq_num, int state);

   private:
    CPU& cpu_;
};

class TrapController {
   public:
    explicit TrapController(CPU& cpu) : cpu_(cpu) {}

    void mret();
    void sret();
    void raiseException(TrapCause cause, CSRValue tval);

   private:
    CPU& cpu_;
};
