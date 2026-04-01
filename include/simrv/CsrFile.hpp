/**
 * @file CsrFile.hpp
 * @brief SimRV CSR register-file interface.
 */
#pragma once

#include "Define.hpp"

class CPU;

class CsrFile {
   public:
    explicit CsrFile(CPU& cpu) : cpu_(cpu) {}

    CSRValue getMstatus(CSRValue mask) const;
    void setMstatus(CSRValue wdata);
    CSRValue read(CSRAddress addr) const;
    void write(CSRAddress addr, CSRValue wdata);

   private:
    CPU& cpu_;
};
