#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <vector>
#include <cstring>
struct TestQueue {
 std::mutex mutex; std::condition_variable changed;
 bool full=false; size_t bytes; std::vector<uint8_t> item;
 explicit TestQueue(size_t n):bytes(n),item(n){}
};
using QueueHandle_t=TestQueue*;
inline QueueHandle_t xQueueCreate(unsigned,size_t n){return new TestQueue(n);}
inline int xQueueSend(QueueHandle_t q,const void *p,uint32_t timeout){
 std::unique_lock<std::mutex> lock(q->mutex);
 if(!timeout && q->full)return 0;
 q->changed.wait(lock,[&]{return !q->full;});
 memcpy(q->item.data(),p,q->bytes);q->full=true;q->changed.notify_all();return 1;
}
inline int xQueueReceive(QueueHandle_t q,void *p,uint32_t timeout){
 std::unique_lock<std::mutex> lock(q->mutex);
 if(!timeout && !q->full)return 0;
 q->changed.wait(lock,[&]{return q->full;});
 memcpy(p,q->item.data(),q->bytes);q->full=false;q->changed.notify_all();return 1;
}
inline void vQueueDelete(QueueHandle_t q){delete q;}
