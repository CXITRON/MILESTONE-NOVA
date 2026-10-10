#include "display/Display.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
namespace nova {
Canvas::~Canvas(){free(pixels_);}
bool Canvas::begin(){pixels_=static_cast<uint16_t*>(calloc(board::width*board::height,2));return pixels_;}
void Canvas::clear(uint16_t c){std::fill(pixels_,pixels_+board::width*board::height,c);}
}
int main(){
 nova::Display d;
 assert(d.begin(40000000,80,false));
 auto *back=d.canvas().pixels();
 assert(d.busy() && d.canRender());
 // Rendering the next buffer cannot corrupt the initial frame the worker is sending.
 d.canvas().clear(0x1234);d.forceStrips(4,32);d.present(1);
 assert(!d.canRender());
 d.wait();
 assert(!d.busy() && d.canRender() && d.canvas().pixels()!=back);
 assert(d.frames()==2 && d.contentFrames()==1);
 assert(transfers.size()==40+12); // second: one video window + 11 UI strips
 for(size_t i=0;i<40;++i)for(auto p:transfers[i])assert(p==nova::color::background);
 for(size_t i=40;i<transfers.size();++i)for(auto p:transfers[i])assert(p==0x1234);
 transfers.clear();startRows.clear();
 d.canvas().clear(0x5555);d.present(2);
 // Future force setting is immutable for a submitted frame.
 d.forceStrips(-1,-1);
 d.canvas().clear(0x3333);d.present(2);
 d.frequency(20000000); // must drain both frames before changing the device
 assert(!d.busy() && d.frames()==4 && d.contentFrames()==2);
 for(size_t i=0;i<12;++i)for(auto p:transfers[i])assert(p==0x5555);
 for(size_t i=12;i<transfers.size();++i)for(auto p:transfers[i])assert(p==0x3333);
 transfers.clear();startRows.clear();
 d.canvas().clear(0x3333);d.present(2);d.wait();
 assert(transfers.empty());
 d.canvas().clear(0x4444);d.present();d.sleep();
 assert(!d.busy());
 // Failed native transfer does not increment completion count or expose another mutable buffer.
 const auto frames=d.frames();nativeFail=true;
 d.canvas().clear(0x7777);d.present();d.wait();
 assert(!d.ready() && !d.busy() && !d.canRender() && d.frames()==frames);
 nativeFail=false;
 // A failed transfer is retried: after the back-off the panel is initialized again and redrawn.
 {
  nova::Display r;
  assert(r.begin(40000000,80,false));
  r.wait();
  nativeFail=true;
  r.canvas().clear(0x2222);r.present();r.wait();
  nativeFail=false;
  assert(!r.ready() && r.failures()==1);
  r.flush(); // within the back-off: nothing happens
  assert(!r.ready() && r.recoveries()==0);
  testMicros+=1100000;
  r.flush();
  assert(r.ready() && r.recoveries()==1);
  transfers.clear();
  r.canvas().clear(0x6666);r.present();r.wait();
  assert(r.canRender() && transfers.size()==40);
  for(auto &t:transfers)for(auto p:t)assert(p==0x6666);
  // A panel that keeps failing is given up on after three attempts.
  nativeFail=true;
  r.canvas().clear(0x1111);r.present();r.wait();
  for(int i=0;i<5;++i){testMicros+=1100000;r.flush();}
  nativeFail=false;
  testMicros+=1100000;r.flush();
  assert(r.failures()>=2);
 }
 std::cout<<"Async frame ownership, bounded queue, immutable mode, drain/sleep and DMA error passed\n";
}
