// Test de robustesse du parseur HID (AUDIT_2026-09.md, M3).
// Toutes les troncatures d un descripteur souris reel + 20 000 descripteurs
// aleatoires, sous AddressSanitizer. Depuis X360Remap/ :
//   g++ -std=c++11 -fsanitize=address -I. -o test_hid_fuzz tests/test_hid_parser_fuzz.cpp hid_parser.cpp && ./test_hid_fuzz
// L ancien parseur echoue ici (heap-buffer-overflow), verifie le 2026-09-23.
#include "../hid_parser.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
bool CALLBACK_HIDParser_FilterHIDReportItem(HID_ReportItem_t* const i){ return i->ItemType==HID_REPORT_ITEM_In; }
int main(){
  // descripteur souris standard, puis toutes ses troncatures
  const uint8_t mouse[]={0x05,0x01,0x09,0x02,0xA1,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x03,0x05,0x01,0x09,0x30,0x09,0x31,0x09,0x38,0x16,0x01,0x80,0x26,0xFF,0x7F,0x75,0x10,0x95,0x03,0x81,0x06,0xC0,0xC0};
  int ok=0;
  for (size_t n=0;n<=sizeof(mouse);n++){
    std::vector<uint8_t> buf(mouse,mouse+n);   // tampon EXACT : ASan voit toute lecture au-dela
    HID_ReportInfo_t* info=0;
    uint8_t r=USB_ProcessHIDReport(buf.data(),(uint16_t)n,&info);
    if(r==HID_PARSE_Successful && info){ ok++; USB_FreeReportInfo(info);} 
  }
  srand(1);
  for(int k=0;k<20000;k++){
    size_t n=rand()%64; std::vector<uint8_t> b(n); for(auto&x:b)x=rand();
    HID_ReportInfo_t* info=0; uint8_t r=USB_ProcessHIDReport(b.data(),(uint16_t)n,&info);
    if(r==HID_PARSE_Successful && info) USB_FreeReportInfo(info);
  }
  printf("troncatures analysees sans erreur memoire (%d succes), fuzz 20000 ok\n",ok);
}
