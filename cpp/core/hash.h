/*
 * hash.h
 * Author: David Wu
 */

#ifndef CORE_HASH_H_
#define CORE_HASH_H_

#include <iostream>

#include "../core/global.h"

namespace Hash
{
  //Splitting of uint64s
  uint32_t highBits(uint64_t x);
  uint32_t lowBits(uint64_t x);
  uint64_t combine(uint32_t hi, uint32_t lo);

  uint64_t basicLCong(uint64_t x);
  uint64_t basicLCong2(uint64_t x);

  //Bit mixers
  uint64_t murmurMix(uint64_t x);
  uint64_t splitMix64(uint64_t x);
  uint64_t rrmxmx(uint64_t x);
  uint64_t nasam(uint64_t x);

  void jenkinsMix(uint32_t& a, uint32_t& b, uint32_t& c);
  uint32_t jenkinsMixSingle(uint32_t a, uint32_t b, uint32_t c);

  uint64_t simpleHash(const char* str);
  uint64_t simpleHash(const int* input, int len);

  //Return value in [0,1) deterministically based on seeded hash. Note that 1.0 might
  //be possible depending on how float math is configured
  double seededHashFloat(const std::string& str, const std::string& seed);
}

//Hash is "little endian" in the sense that if you printed out hash1, then hash0, you would
//get the standard 128 bit hash string as would be output by something like MD5, as well as
//for comparison.
struct Hash128
{
  uint64_t hash0;
  uint64_t hash1;

  Hash128();
  Hash128(uint64_t hash0, uint64_t hash1);

  bool operator<(const Hash128 other) const;
  bool operator>(const Hash128 other) const;
  bool operator<=(const Hash128 other) const;
  bool operator>=(const Hash128 other) const;
  bool operator==(const Hash128 other) const;
  bool operator!=(const Hash128 other) const;

  Hash128 operator^(const Hash128 other) const;
  Hash128 operator|(const Hash128 other) const;
  Hash128 operator&(const Hash128 other) const;
  Hash128& operator^=(const Hash128 other);
  Hash128& operator|=(const Hash128 other);
  Hash128& operator&=(const Hash128 other);

  friend std::ostream& operator<<(std::ostream& out, const Hash128 other);
  std::string toString() const;
  static Hash128 ofString(const std::string& s);

  // mix Hash with int
  static Hash128 mixInt(Hash128 h, int64_t t);

};

inline Hash128::Hash128()
:hash0(0),hash1(0)
{}

inline Hash128::Hash128(uint64_t h0, uint64_t h1)
:hash0(h0),hash1(h1)
{}



inline bool Hash128::operator==(const Hash128 other) const
{return hash0 == other.hash0 && hash1 == other.hash1;}

inline bool Hash128::operator!=(const Hash128 other) const
{return hash0 != other.hash0 || hash1 != other.hash1;}

inline bool Hash128::operator>(const Hash128 other) const
{
  if(hash1 > other.hash1) return true;
  if(hash1 < other.hash1) return false;
  return hash0 > other.hash0;
}
inline bool Hash128::operator>=(const Hash128 other) const
{
  if(hash1 > other.hash1) return true;
  if(hash1 < other.hash1) return false;
  return hash0 >= other.hash0;
}
inline bool Hash128::operator<(const Hash128 other) const
{
  if(hash1 < other.hash1) return true;
  if(hash1 > other.hash1) return false;
  return hash0 < other.hash0;
}
inline bool Hash128::operator<=(const Hash128 other) const
{
  if(hash1 < other.hash1) return true;
  if(hash1 > other.hash1) return false;
  return hash0 <= other.hash0;
}

inline Hash128 Hash128::operator^(const Hash128 other) const {
  return Hash128(hash0 ^ other.hash0, hash1 ^ other.hash1);
}
inline Hash128 Hash128::operator|(const Hash128 other) const {
  return Hash128(hash0 | other.hash0, hash1 | other.hash1);
}
inline Hash128 Hash128::operator&(const Hash128 other) const {
  return Hash128(hash0 & other.hash0, hash1 & other.hash1);
}
inline Hash128& Hash128::operator^=(const Hash128 other) {
  hash0 ^= other.hash0;
  hash1 ^= other.hash1;
  return *this;
}
inline Hash128& Hash128::operator|=(const Hash128 other) {
  hash0 |= other.hash0;
  hash1 |= other.hash1;
  return *this;
}
inline Hash128& Hash128::operator&=(const Hash128 other) {
  hash0 &= other.hash0;
  hash1 &= other.hash1;
  return *this;
}


#endif  // CORE_HASH_H_
