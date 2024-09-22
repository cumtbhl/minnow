#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <algorithm>

using namespace std;

RetransmissionTimer& RetransmissionTimer::active() noexcept
{
  is_active_ = true;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::timeout() noexcept
{
  RTO_ <<= 1;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::reset() noexcept
{
  time_passed_ = 0;
  return *this;
}

RetransmissionTimer& RetransmissionTimer::tick( uint64_t ms_since_last_tick ) noexcept
{
  time_passed_ += is_active_ ? ms_since_last_tick : 0;
  return *this;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  /*  input_ 是发送方的数据流
      bytes_reader 是用来从 input_ 中读取数据的对象 */
  Reader& bytes_reader = input_.reader();

  /*  bytes_reader.is_finished() 返回 true（表示流中的数据已经读完）时
      fin_flag_ 就会被设置为 true，表示 TCP 连接应该进入关闭阶段，并且需要发送一个带有 FIN 标志的报文 */
  fin_flag_ |= bytes_reader.is_finished(); 

  //  检查连接是否已经结束
  if ( sent_fin_ )
    return; 

  //  计算窗口大小
  const size_t window_size = wnd_size_ == 0 ? 1 : wnd_size_;

  //  不断从input_读取数据 放入 payload
  for ( string payload {}; num_bytes_in_flight_ < window_size && !sent_fin_; payload.clear() ) {
    //  从 input_ 流中“窥视”当前可用的数据，并将其存储在 bytes_view 
    string_view bytes_view = bytes_reader.peek();

    //  如果 连接已建立 (sent_syn_) 且 当前流中没有数据可读 (bytes_view.empty())，并且不需要发送 FIN 报文 (!fin_flag_)
    if ( sent_syn_ && bytes_view.empty() && !fin_flag_ )
      break;

    //  组装payload，直到达到 报文长度限制 或 窗口上限
    while ( payload.size() + num_bytes_in_flight_ + ( !sent_syn_ ) < window_size
            && payload.size() < TCPConfig::MAX_PAYLOAD_SIZE ) { 
      //  如果所有可用的数据都已经被读取
      if ( bytes_view.empty() || fin_flag_ )
        break; 

      //  如果当前准备组装进 payload 的 bytes_view.size() > available_size,则需要截断 bytes_view
      if ( const uint64_t available_size
           = min( TCPConfig::MAX_PAYLOAD_SIZE - payload.size(),
                  window_size - ( payload.size() + num_bytes_in_flight_ + ( !sent_syn_ ) ) );
           bytes_view.size() > available_size ) 
        //  移除 bytes_view 末尾超出 available_size 的部分,仅保留前面 available_size 个字符
        bytes_view.remove_suffix( bytes_view.size() - available_size );

      //  更新payload
      payload.append( bytes_view );
      bytes_reader.pop( bytes_view.size() );

      //  从流中弹出字符后要检查流是否关闭
      fin_flag_ |= bytes_reader.is_finished();
      bytes_view = bytes_reader.peek();
    }

    //  生成一个TCPSenderMessage msg
    auto& msg = outstanding_bytes_.emplace(
      make_message( next_seqno_, move( payload ), sent_syn_ ? syn_flag_ : true, fin_flag_ ) );
    
    /*  如果SYN 已经发送过，则margin = 1
        如果SYN 没有发送过，则margin = 0  */
    const size_t margin = sent_syn_ ? syn_flag_ : 0;

    //  决定当前组装的msg 能否发送 FIN标志
    if ( fin_flag_ && ( msg.sequence_length() - margin ) + num_bytes_in_flight_ > window_size )
      msg.FIN = false;    
    else if ( fin_flag_ ) 
      sent_fin_ = true;
      
    // 最后再计算真实的报文长度
    const size_t correct_length = msg.sequence_length() - margin;

    num_bytes_in_flight_ += correct_length;
    next_seqno_ += correct_length;
    sent_syn_ = true;
    transmit( msg );
    if ( correct_length != 0 )
      timer_.active();
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return make_message( next_seqno_, {}, false );
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  wnd_size_ = msg.window_size;
  //  没有 ackno 的报文通常不会确认新的字节，而可能只是更新窗口大小
  if ( !msg.ackno.has_value() ) {
    if ( msg.window_size == 0 )
      input_.set_error();
    return;
  }

  const uint64_t excepting_seqno = msg.ackno->unwrap( isn_, next_seqno_ );
  /*  如果接收方的确认号（ackno）超出发送方当前的发送范围
      这个确认就没有意义了，发送方就不需要对这个确认进行进一步处理，因此直接返回。  */
  if ( excepting_seqno > next_seqno_ ) 
    return;                            

  bool is_acknowledged = false; // 用于判断确认是否发生
  while ( !outstanding_bytes_.empty() ) {
    auto& buffered_msg = outstanding_bytes_.front();
    
    // 对方期待的下一字节不大于队首的字节序号，或者队首分组只有部分字节被确认
    if ( const uint64_t final_seqno = acked_seqno_ + buffered_msg.sequence_length() - buffered_msg.SYN;
         excepting_seqno <= acked_seqno_ || excepting_seqno < final_seqno )
      break; // 这种情况下不会更改缓冲队列

    is_acknowledged = true; // 表示有字节被确认
    num_bytes_in_flight_ -= buffered_msg.sequence_length() - syn_flag_;
    acked_seqno_ += buffered_msg.sequence_length() - syn_flag_;
    // 最后检查 syn 是否被确认
    syn_flag_ = sent_syn_ ? syn_flag_ : excepting_seqno <= next_seqno_;
    outstanding_bytes_.pop();
  }

  if ( is_acknowledged ) {
    // 如果全部分组都被确认，那就停止计时器
    if ( outstanding_bytes_.empty() )
      timer_ = RetransmissionTimer( initial_RTO_ms_ );
    else // 否则就只重启计时器
      timer_ = move( RetransmissionTimer( initial_RTO_ms_ ).active() );
    retransmission_cnt_ = 0; // 因为要重置 RTO 值，故直接更换新对象
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( timer_.tick( ms_since_last_tick ).is_expired() ) {
    transmit( outstanding_bytes_.front() ); // 只传递队首元素
    if ( wnd_size_ == 0 )
      timer_.reset();
    else
      timer_.timeout().reset();
    ++retransmission_cnt_;
  }
}

TCPSenderMessage TCPSender::make_message( uint64_t seqno, string payload, bool SYN, bool FIN ) const
{
  return { .seqno = Wrap32::wrap( seqno, isn_ ),
           .SYN = SYN,
           .payload = move( payload ),
           .FIN = FIN,
           .RST = input_.reader().has_error() };
}

uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return num_bytes_in_flight_;
}

uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmission_cnt_;
}
