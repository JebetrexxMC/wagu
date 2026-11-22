// 5.02.48: the open.mp component never sets a SampGate on its MPServer (omp-server itself is the PC side),
// but server.cpp calls the gate behind "if(m_pGate)". These stubs keep the component free of the full gate
// (sampgate.cpp + RakNet), which is what made omp-server refuse mp-crossplay.so with
// "undefined symbol: _ZN8SampGate6OnSampEiPKhi".
#include "net/sampgate.h"
bool SampGate::OnChat(int, const char*) { return false; }
bool SampGate::OnSamp(int, const uint8_t*, int) { return false; }
bool SampGate::OnDialog(int, uint16_t, uint8_t, int16_t, const char*) { return false; }
void SampGate::OnPhonePkt(int, const uint8_t*, int) {}
