#pragma once

#include "reassembler.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include <optional>

class TCPReceiver
{
public:
  // Construct with given Reassembler
  explicit TCPReceiver( Reassembler&& reassembler ) : reassembler_( std::move( reassembler ) ) {}

  //该方法接收 TCPSenderMessage 类型的消息，
  //并将其有效负载插入到 Reassembler 中，确保数据按正确的流索引进行重组。
  void receive( TCPSenderMessage message );

  //该方法生成并返回一个 TCPReceiverMessage，用于发送给对端的 TCP 发送者
  TCPReceiverMessage send() const;

  //这些方法提供了对重组器状态的访问，以便进行读取和写入操作
  const Reassembler& reassembler() const { return reassembler_; }
  Reader& reader() { return reassembler_.reader(); }
  const Reader& reader() const { return reassembler_.reader(); }
  const Writer& writer() const { return reassembler_.writer(); }

private:
  Reassembler reassembler_;
  //表示接收方是否已经接收到有效的初始序列号。
  std::optional<Wrap32> ISN_ {};
};
