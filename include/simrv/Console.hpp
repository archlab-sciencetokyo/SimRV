/**
 * @file Console.hpp
 * @brief SimRV declarations.
 */
#pragma once
#include "Define.hpp"
#include "State.hpp"
class Console {
   public:
    Console();
    Word console_read(Address offset);
    void console_write(CPU*, Address, Word);
    int recieve_input();
    int MC_recieve_input();

    Byte* mmem;  // main memory

    QueueState* Queue; /* Queue of Console */

    Word DeviceFeaturesSel;
    Word DriverFeatures;
    Word DriverFeaturesSel;
    Word InterruptStatus;
    Word Status;
    Word QueueSel;
    Word QueueNum;

    Byte cons_fifo;
    Byte fifo_en;

    // struct QueueState Queue[CONSOLE_MAX_QUEUE_NUM];
   private:
};
