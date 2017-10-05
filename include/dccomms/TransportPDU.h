#ifndef DCCOMMS_TRANSPORTPDU_H
#define DCCOMMS_TRANSPORTPDU_H
#include <boost/shared_ptr.hpp>

#include <dccomms/Packet.h>

namespace dccomms {

class TransportPDU;

typedef boost::shared_ptr<TransportPDU> TransportPDUPtr;

class TransportPDU : public Packet {
public:
  static int OverheadSize;
  static TransportPDUPtr BuildTransportPDU();

  TransportPDU();
  uint8_t GetSeqNum();
  void SetSeqNum(uint8_t seq);
  void IncSeqNum();

  void SetBuffer(void *);
  void BufferUpdated();
  void SetPayload(void *pyload, int size);

  void CopyFromRawBuffer(void *buffer);
  uint8_t *GetPayloadBuffer();
  uint32_t GetPayloadSize();
  int GetPacketSize();
  void Read(IStream *comms);

private:
  uint8_t *_nseq, *_payload;

  uint32_t _payloadSize;

  void _Init();
  void _InitPointers();
};
}

#endif // DCCOMMS_TRANSPORTPDU_H
