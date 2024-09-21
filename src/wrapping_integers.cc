#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  //因为 n 是 uint64_t ，当其被赋值给 Wrap32 类型的对象时，会发生隐式类型转换。
  //这个转换通常是通过截断高位，只保留低32位。
  //通过将结果截断为32位，实际上已经实现了回绕效果
  return zero_point + n;
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  //定义 upper 是一个 uint64_t 类型的常量，值为 2^32
  constexpr uint64_t upper = static_cast<uint64_t>( UINT32_MAX ) + 1;
  //将checkpoint相对于zero_point进行包裹处理，得到ckpt_mod
  const uint32_t ckpt_mod = Wrap32::wrap( checkpoint, zero_point ).raw_value_;
  //计算在当前"包裹"体系下，从checkpoint到raw_value_需要"走过"的距离。得到distance
  uint32_t distance = raw_value_ - ckpt_mod;
  //如果distance很小（比如小于UINT32_MAX / 2）
  //或者checkpoint + distance 在64位空间中仍然小于即upper
  //则我们可以直接相加
  if ( distance <= ( upper >> 1 ) || checkpoint + distance < upper )
    return checkpoint + distance;
  //否则，我们需要从结果中减去UINT32_MAX + 1（即upper），以模拟32位空间中的“包裹”行为。
  return checkpoint + distance - upper;
}
