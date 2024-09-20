#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  //checkpoint 表示到正在期待的下一个字节的序号，用于计算message.seqno转换成abso_seqno_
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();

  //检查RST，有值的话需要关闭当前连接
  if ( message.RST ) {
    reassembler_.reader().set_error();
  } 

  //拦截如果接收到的序列号等于 ISN 的情况。
  else if ( checkpoint > 0 && checkpoint <= UINT32_MAX && message.seqno == ISN_ )
    return; 

  //查看ISN_是否有值，判断该messag是否为首次消息
  if ( !ISN_.has_value() ) {
    //查看SYN的值，判断这个首次消息是否合法
    if ( !message.SYN )
      return;
    //如果合法，则设置ISN_为message.seqno
    ISN_ = message.seqno;
  }

  //根据ISN_和checkpoint计算绝对序列号计算message的绝对序列号abso_seqno_
  const uint64_t abso_seqno_ = message.seqno.unwrap( *ISN_, checkpoint );

  //在insert函数中，abso_seqno_需要转变成stream index
  //当 abso_seqno_ 为 0 时，表示接收的消息是流的第一个部分。此时，流索引也是 0，因此可以直接插入
  //当 abso_seqno_ 不为 0 时，表示已经有数据接收过，流索引应为 abso_seqno_ - 1。
  //这样插入的负载将正确地放置在流的前一个位置，确保数据流的顺序性。
  reassembler_.insert( abso_seqno_ == 0 ? abso_seqno_ : abso_seqno_ - 1, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  //checkpoint 表示到正在期待的下一个字节的序号
  const uint64_t checkpoint = reassembler_.writer().bytes_pushed() + ISN_.has_value();

  //计算window_size
  const uint64_t capacity = reassembler_.writer().available_capacity();
  const uint16_t wnd_size = capacity > UINT16_MAX ? UINT16_MAX : capacity;

  //处理初始序列号的情况：
  //将返回一个 TCPReceiverMessage，
  //其中ackno为空，window_size为 wnd_size，以及当前接收器是否有错误。
  if ( !ISN_.has_value() )
    return { {}, wnd_size, reassembler_.writer().has_error() };

  //处理 ISN 存在的情况：
  //通过ISN_、checkpoint、reassembler_.writer().is_closed（）计算ackno
  return { Wrap32::wrap( checkpoint + reassembler_.writer().is_closed(), *ISN_ ),
           wnd_size,
           reassembler_.writer().has_error() };
}
