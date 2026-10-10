#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include <algorithm>
#include <jpeglib.h>
#include "media/JpegImage.h"
int main() {
 for (unsigned side : {160u, 200u, 240u}) for (bool progressive : {false, true}) {
  jpeg_compress_struct c{}; jpeg_error_mgr e{}; c.err=jpeg_std_error(&e); jpeg_create_compress(&c);
  unsigned char *jpeg=nullptr; unsigned long bytes=0; jpeg_mem_dest(&c,&jpeg,&bytes);
  c.image_width=side; c.image_height=side; c.input_components=3; c.in_color_space=JCS_RGB;
  jpeg_set_defaults(&c); jpeg_set_quality(&c,85,TRUE); if(progressive) jpeg_simple_progression(&c);
  jpeg_start_compress(&c,TRUE); std::vector<uint8_t> line(side*3);
  while(c.next_scanline<c.image_height) {
   for(unsigned x=0;x<side;++x) { line[x*3]=uint8_t(x*255/side); line[x*3+1]=uint8_t(c.next_scanline*255/side); line[x*3+2]=uint8_t((x+c.next_scanline)*127/side); }
   JSAMPROW p=line.data(); jpeg_write_scanlines(&c,&p,1);
  }
  jpeg_finish_compress(&c); jpeg_destroy_compress(&c);
  void *encoded=nullptr,*work=nullptr,*raw=nullptr;
  assert(!posix_memalign(&encoded,16,bytes+16)); assert(!posix_memalign(&work,16,65536)); assert(!posix_memalign(&raw,16,side*side*2+16));
  memcpy(encoded,jpeg,bytes); memset(static_cast<uint8_t*>(encoded)+bytes,0,16); free(jpeg);
  std::vector<uint16_t> reference(side*side);
  assert(nova::decodeJpeg565(static_cast<uint8_t*>(encoded),bytes,reference.data(),side,side,static_cast<uint8_t*>(work),65536));
  auto *out=static_cast<uint16_t*>(raw);
  assert(nova::decodeJpeg565(static_cast<uint8_t*>(encoded),bytes,out,side,side,static_cast<uint8_t*>(work),65536,true));
  double sum=0; unsigned worst=0;
  for(unsigned i=0;i<side*side;++i) for(unsigned shift : {0u,5u,11u}) {
   const unsigned mask=shift==5?63:31,scale=shift==5?4:8;
   unsigned diff=unsigned(std::abs(int((out[i]>>shift)&mask)-int((reference[i]>>shift)&mask)))*scale;
   sum+=diff; worst=std::max(worst,diff);
  }
  assert(sum/(side*side*3)<4 && worst<=32);
  // Misalignment must safely use the legacy decoder with exactly matching pixels.
  assert(nova::decodeJpeg565(static_cast<uint8_t*>(encoded),bytes,out+1,side,side,static_cast<uint8_t*>(work),65536,true));
  assert(std::equal(reference.begin(),reference.end(),out+1));
  assert(!nova::decodeJpeg565(static_cast<uint8_t*>(encoded),bytes,out,side-1,side,static_cast<uint8_t*>(work),65536,true));
  if (!progressive) {
   // JPEGDEC indexes fixed tables with header values. Edited headers must stay out of its fast
   // path (UBSan aborts on an out-of-range index or a zero MCU size) and still decode safely.
   auto *data=static_cast<uint8_t*>(encoded);
   const auto find=[&](uint8_t marker,size_t from) { for(size_t i=from;i+4<bytes;++i) if(data[i]==0xFF&&data[i+1]==marker) return i; return size_t(0); };
   const size_t sof=find(0xC0,2), dht=find(0xC4,2), sos=find(0xDA,2);
   assert(sof&&dht&&sos);
   const uint8_t sofSampling=data[sof+10], dhtId=data[dht+4], sosTables=data[sos+7];
   const uint8_t dhtCount=data[dht+5];
   for(int edit=0;edit<4;++edit) {
    data[sof+10]=edit==0?0:sofSampling; data[dht+4]=edit==1?3:dhtId; data[sos+7]=edit==2?0x33:sosTables; data[dht+5]=edit==3?255:dhtCount;
    nova::decodeJpeg565(data,bytes,out,side,side,static_cast<uint8_t*>(work),65536,true);
   }
   data[sof+10]=sofSampling; data[dht+4]=dhtId; data[sos+7]=sosTables; data[dht+5]=dhtCount;
   srand(side);
   std::vector<uint8_t> original(data,data+bytes);
   for(int round=0;round<300;++round) {
    memcpy(data,original.data(),bytes);
    for(int edit=0;edit<4;++edit) data[rand()%bytes]=uint8_t(rand());
    nova::decodeJpeg565(data,bytes,out,side,side,static_cast<uint8_t*>(work),65536,true);
   }
   memcpy(data,original.data(),bytes);
  }
  memset(encoded,0,bytes);
  assert(!nova::decodeJpeg565(static_cast<uint8_t*>(encoded),bytes,out,side,side,static_cast<uint8_t*>(work),65536,true));
  free(encoded); free(raw); free(work);
 }
 puts("Real JPEGDEC baseline/progressive fallback, RGB565 accuracy, dimensions, alignment and malformed input passed");
}
