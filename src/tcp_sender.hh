#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <functional>
#include <queue>
#include <utility>

// 超时计时器
class RetransmissionTimer
{
public:
  RetransmissionTimer( uint64_t initial_RTO_ms ) : RTO_( initial_RTO_ms ) {}

  //  检查定时器是否已经到达超时时间
  bool is_expired() const noexcept { return is_active_ && time_passed_ >= RTO_; }

  //  检查定时器是否处于活动状态
  bool is_active() const noexcept { return is_active_; }

  //  激活定时器
  RetransmissionTimer& active() noexcept;

  /*  重传超时时间（RTO_）翻倍（即实现所谓的指数退避机制）。
      每次定时器超时后，超时时间会加倍，以避免频繁的重传加剧网络拥堵。  */
  RetransmissionTimer& timeout() noexcept;

  //  重置计时器状态：将time_passed_ 置0
  RetransmissionTimer& reset() noexcept;

  //  更新定时器状态: time_passed_ += ms_since_last_tick
  RetransmissionTimer& tick( uint64_t ms_since_last_tick ) noexcept;

private:
  /*  当前的重传超时时间（Retransmission Timeout，单位是毫秒）。
      该变量会被动态调整，初始值通过构造函数传入。  */
  uint64_t RTO_;

  //记录自定时器激活后流逝的时间（单位是毫秒）。每次 tick() 调用时会增加此值。
  uint64_t time_passed_ {};

  //标记定时器是否正在运行
  bool is_active_ {};
};

class TCPSender
{
public:
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), timer_( initial_RTO_ms )
  {}

  //  生成一个空的 TCP 发送器消息
  TCPSenderMessage make_empty_message() const;

  //  处理来自接收方的消息
  void receive( const TCPReceiverMessage& msg );

  /*  定义了一个别名 TransmitFunction
      它是一个函数类型（这种函数接受一个 const TCPSenderMessage& 参数，并且不返回任何值。） */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /*  push()：从字节流中读取尽可能多的数据，并生成相应的 TCP 段，将其发送出去
      transmit是一个符合 TransmitFunction 类型的函数
      当 push 被调用时，它可以通过这个传递进来的 transmit 函数来发送消息. */
  void push( const TransmitFunction& transmit );

  /*  更新内部的重传定时器，并检查是否需要重传。 */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  //  返回当前尚未被确认的数据段的总字节数。
  uint64_t sequence_numbers_in_flight() const;  

  /*  返回连续重传的次数
      用于判断是否需要终止连接，或是否应触发指数退避  */
  uint64_t consecutive_retransmissions() const; // How many consecutive *re*transmissions have happened?

  Writer& writer() { return input_.writer(); }
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }

private:
  //创建一个 TCPSenderMessage
  TCPSenderMessage make_message( uint64_t seqno, std::string payload, bool SYN, bool FIN = false ) const;

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;

  uint16_t wnd_size_ { 1 }; // 初始假定窗口大小为 1
  uint64_t next_seqno_ {};  // 待发送的下一个字节序号
  uint64_t acked_seqno_ {}; // 已确认的字节序号

  //  标记 是否需要在接下来的 TCP 报文中发送 SYN 标志
  bool syn_flag_ {};

  //  标记 是否需要在接下来的 TCP 报文中发送 FIN 标志
  bool fin_flag_ {};

  //  标记 是否已经发送过 SYN 标志
  bool sent_syn_ {}; 

  //  标记 是否已经发送过 FIN 标志
  bool sent_fin_ {};

  RetransmissionTimer timer_;
  uint64_t retransmission_cnt_ {};

  std::queue<TCPSenderMessage> outstanding_bytes_ {};
  uint64_t num_bytes_in_flight_ {};
};
