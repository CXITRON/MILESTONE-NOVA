#pragma once
#include "FreeRTOS.h"
#include <thread>
#include <chrono>
struct TaskThread { std::thread thread; explicit TaskThread(void(*f)(void*),void*p):thread(f,p){} ~TaskThread(){if(thread.joinable())thread.join();} };
using TaskHandle_t=TaskThread*;
inline int xTaskCreatePinnedToCore(void(*f)(void*),const char*,unsigned,void*p,unsigned,TaskHandle_t *h,int){*h=new TaskThread(f,p);return pdPASS;}
inline void vTaskDelay(unsigned n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
inline void vTaskDelete(TaskHandle_t){}
