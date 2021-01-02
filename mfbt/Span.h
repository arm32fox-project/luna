///////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2015 Microsoft Corporation. All rights reserved.
// Copyright (c) 2020 Moonchild Productions. All rights reserved.
//
// This code is licensed under the MIT License (MIT).
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
///////////////////////////////////////////////////////////////////////////////

// Adapted from https://github.com/Microsoft/GSL/blob/3819df6e378ffccf0e29465afe99c3b324c2aa70/include/gsl/span
// and https://github.com/Microsoft/GSL/blob/3819df6e378ffccf0e29465afe99c3b324c2aa70/include/gsl/gsl_util

#ifndef mozilla_Span_h
#define mozilla_Span_h

#include "mozilla/Array.h"
#include "mozilla/Assertions.h"
#include "mozilla/Casting.h"
#include "mozilla/IntegerTypeTraits.h"
#include "mozilla/Move.h"
#include "mozilla/TypeTraits.h"
#include "mozilla/UniquePtr.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>

#ifdef _MSC_VER
#pragma warning(push)

// turn off some warnings that are noisy about our MOZ_RELEASE_ASSERT statements
#pragma warning(disable : 4127) // conditional expression is constant

// blanket turn off warnings from CppCoreCheck for now
// so people aren't annoyed by them when running the tool.
// more targeted suppressions will be added in a future update to the GSL
#pragma warning(disable : 26481 26482 26483 26485 26490 26491 26492 26493 26495)

#endif            // _MSC_VER

namespace mozilla {

// Stuff from gsl_util

// narrow_cast(): a searchable way to do narrowing casts of values
template<class T, class U>
inline T
narrow_cast(U&& u)
{
  return static_cast<T>(mozilla::Forward<U>(u));
}

// end gsl_util

// [views.constants], constants
// This was -1 in gsl::span, but using size_t for sizes instead of ptrdiff_t
// and reserving a magic value that realistically doesn't occur in
// compile-time-constant Span sizes makes things a lot less messy in terms of
// comparison between signed and unsigned.
const size_t dynamic_extent = mozilla::MaxValue<size_t>::value;

template<class ElementType, size_t Extent = dynamic_extent>
class Span;

// implementation details
namespace span_details {

// C++14 types that we don't have because we build as C++11.
template<class T>
using remove_cv_t = typename mozilla::RemoveCV<T>::Type;
template<class T>
using remove_const_t = typename mozilla::RemoveConst<T>::Type;
template<bool B, class T, class F>
using conditional_t = typename mozilla::Conditional<B, T, F>::Type;
template<class T>
using add_pointer_t = typename mozilla::AddPointer<T>::Type;
template<bool B, class T = void>
using enable_if_t = typename mozilla::EnableIf<B, T>::Type;

template<class T>
struct is_span_oracle : mozilla::FalseType
{
};

template<class ElementType, size_t Extent>
struct is_span_oracle<mozilla::Span<ElementType, Extent>> : mozilla::TrueType
{
};

template<class T>
struct is_span : public is_span_oracle<remove_cv_t<T>>
{
};

template<class T>
struct is_std_array_oracle : mozilla::FalseType
{
};

template<class ElementType, size_t Extent>
struct is_std_array_oracle<std::array<ElementType, Extent>> : mozilla::TrueType
{
};

template<class T>
struct is_std_array : public is_std_array_oracle<remove_cv_t<T>>
{
};

template<size_t From, size_t To>
struct is_allowed_extent_conversion
  : public mozilla::IntegralConstant<bool,
                                  From == To ||
                                    From == mozilla::dynamic_extent ||
                                    To == mozilla::dynamic_extent>
{
};

template<class From, class To>
struct is_allowed_element_type_conversion
  : public mozilla::IntegralConstant<bool, mozilla::IsConvertible<From (*)[], To (*)[]>::value>
{
};

template<class Span, bool IsConst>
class span_iterator
{
  using element_type_ = typename Span::element_type;

public:
  using iterator_category = std::random_access_iterator_tag;
  using value_type = remove_const_t<element_type_>;
  using difference_type = typename Span::index_type;

  using reference = conditional_t<IsConst, const element_type_, element_type_>&;
  using pointer = add_pointer_t<reference>;

  span_iterator() : span_iterator(nullptr, 0) {}

  span_iterator(const Span* span, typename Span::index_type index)
    : span_(span)
    , index_(index)
  {
    MOZ_RELEASE_ASSERT(span == nullptr ||
                       (index_ >= 0 && index <= span_->Length()));
  }

  friend class span_iterator<Span, true>;
  MOZ_IMPLICIT span_iterator(const span_iterator<Span, false>& other)
    : span_iterator(other.span_, other.index_)
  {
  }

  span_iterator<Span, IsConst>&
  operator=(const span_iterator<Span, IsConst>&) = default;

  reference operator*() const
  {
    MOZ_RELEASE_ASSERT(span_);
    return (*span_)[index_];
  }

  pointer operator->() const
  {
    MOZ_RELEASE_ASSERT(span_);
    return &((*span_)[index_]);
  }

  span_iterator& operator++()
  {
    MOZ_RELEASE_ASSERT(span_ && index_ >= 0 && index_ < span_->Length());
    ++index_;
    return *this;
  }

  span_iterator operator++(int)
  {
    auto ret = *this;
    ++(*this);
    return ret;
  }

  span_iterator& operator--()
  {
    MOZ_RELEASE_ASSERT(span_ && index_ > 0 && index_ <= span_->Length());
    --index_;
    return *this;
  }

  span_iterator operator--(int)
  {
    auto ret = *this;
    --(*this);
    return ret;
  }

  span_iterator
  operator+(difference_type n) const
  {
    auto ret = *this;
    return ret += n;
  }

  span_iterator& operator+=(difference_type n)
  {
    MOZ_RELEASE_ASSERT(span_ && (index_ + n) >= 0 &&
                       (index_ + n) <= span_->Length());
    index_ += n;
    return *this;
  }

  span_iterator
  operator-(difference_type n) const
  {
    auto ret = *this;
    return ret -= n;
  }

  span_iterator& operator-=(difference_type n)
  {
    return *this += -n;
  }

  difference_type
  operator-(const span_iterator& rhs) const
  {
    MOZ_RELEASE_ASSERT(span_ == rhs.span_);
    return index_ - rhs.index_;
  }

  reference operator[](difference_type n) const
  {
    return *(*this + n);
  }

  friend bool operator==(const span_iterator& lhs,
                                   const span_iterator& rhs)
  {
    return lhs.span_ == rhs.span_ && lhs.index_ == rhs.index_;
  }

  friend bool operator!=(const span_iterator& lhs,
                                   const span_iterator& rhs)
  {
    return !(lhs == rhs);
  }

  friend bool operator<(const span_iterator& lhs, const span_iterator& rhs)
  {
    MOZ_RELEASE_ASSERT(lhs.span_ == rhs.span_);
    return lhs.index_ < rhs.index_;
  }

  friend bool operator<=(const span_iterator& lhs, const span_iterator& rhs)
  {
    return !(rhs < lhs);
  }

  friend bool operator>(const span_iterator& lhs, const span_iterator& rhs)
  {
    return rhs < lhs;
  }

  friend bool operator>=(const span_iterator& lhs, const span_iterator& rhs)
  {
    return !(rhs > lhs);
  }

  void swap(span_iterator& rhs)
  {
    std::swap(index_, rhs.index_);
    std::swap(span_, rhs.span_);
  }

protected:
  const Span* span_;
  size_t index_;
};

template<class Span, bool IsConst>
inline span_iterator<Span, IsConst>
operator+(typename span_iterator<Span, IsConst>::difference_type n,
          const span_iterator<Span, IsConst>& rhs)
{
  return rhs + n;
}

template<size_t Ext>
class extent_type
{
public:
  using index_type = size_t;

  static_assert(Ext >= 0, "A fixed-size Span must be >= 0 in size.");

  extent_type() {}

  template<index_type Other>
  MOZ_IMPLICIT extent_type(extent_type<Other> ext)
  {
    static_assert(
      Other == Ext || Other == dynamic_extent,
      "Mismatch between fixed-size extent and size of initializing data.");
    MOZ_RELEASE_ASSERT(ext.size() == Ext);
  }

  MOZ_IMPLICIT extent_type(index_type length)
  {
    MOZ_RELEASE_ASSERT(length == Ext);
  }

  index_type size() const { return Ext; }
};

template<>
class extent_type<dynamic_extent>
{
public:
  using index_type = size_t;

  template<index_type Other>
  explicit extent_type(extent_type<Other> ext)
    : size_(ext.size())
  {
  }

  explicit extent_type(index_type length)
    : size_(length)
  {
  }

  index_type size() const { return size_; }

private:
  index_type size_;
};
} // namespace span_details

/**
 * Span - slices for C++
 *
 * Span implements Rust's slice concept for C++. It's called "Span" instead of
 * "Slice" to follow the naming used in C++ Core Guidelines.
 *
 * A Span wraps a pointer and a length that identify a non-owning view to a
 * contiguous block of memory of objects of the same type. Various types,
 * including (pre-decay) C arrays, XPCOM strings, nsTArray, mozilla::Array,
 * mozilla::Range and contiguous standard-library containers, auto-convert
 * into Spans when attempting to pass them as arguments to methods that take
 * Spans. MakeSpan() functions can be used for explicit conversion in other
 * contexts. (Span itself autoconverts into mozilla::Range.)
 *
 * Like Rust's slices, Span provides safety against out-of-bounds access by
 * performing run-time bound checks. However, unlike Rust's slices, Span
 * cannot provide safety against use-after-free.
 *
 * (Note: Span is like Rust's slice only conceptually. Due to the lack of
 * ABI guarantees, you should still decompose spans/slices to raw pointer
 * and length parts when crossing the FFI.)
 *
 * In addition to having constructors and MakeSpan() functions that take
 * various well-known types, a Span for an arbitrary type can be constructed
 * (via constructor or MakeSpan()) from a pointer and a length or a pointer
 * and another pointer pointing just past the last element.
 *
 * A Span<const char> can be obtained for const char* pointing to a
 * zero-terminated C string using the MakeCStringSpan() function. A
 * corresponding implicit constructor does not exist in order to avoid
 * accidental construction in cases where const char* does not point to a
 * zero-terminated C string.
 *
 * Span has methods that follow the Mozilla naming style and methods that
 * don't. The methods that follow the Mozilla naming style are meant to be
 * used directly from Mozilla code. The methods that don't are meant for
 * integration with C++11 range-based loops and with meta-programming that
 * expects the same methods that are found on the standard-library
 * containers. For example, to decompose a Span into its parts in Mozilla
 * code, use Elements() and Length() (as with nsTArray) instead of data()
 * and size() (as with std::vector).
 *
 * The pointer and length wrapped by a Span cannot be changed after a Span has
 * been created. When new values are required, simply create a new Span. Span
 * has a method called Subspan() that works analogously to the Substring()
 * method of XPCOM strings taking a start index and an optional length. As a
 * Mozilla extension (relative to Microsoft's gsl::span that mozilla::Span is
 * based on), Span has methods From(start), To(end) and FromTo(start, end)
 * that correspond to Rust's &slice[start..], &slice[..end] and
 * &slice[start..end], respectively. (That is, the end index is the index of
 * the first element not to be included in the new subspan.)
 *
 * When indicating a Span that's only read from, const goes inside the type
 * parameter. Don't put const in front of Span. That is:
 * size_t ReadsFromOneSpanAndWritesToAnother(Span<const uint8_t> aReadFrom,
 *                                           Span<uint8_t> aWrittenTo);
 *
 * Any Span<const T> can be viewed as Span<const uint8_t> using the function
 * AsBytes(). Any Span<T> can be viewed as Span<uint8_t> using the function
 * AsWritableBytes().
 */
template<class ElementType, size_t Extent>
class Span
{
public:
  // constants and types
  using element_type = ElementType;
  using index_type = size_t;
  using pointer = element_type*;
  using reference = element_type&;

  using iterator =
    span_details::span_iterator<Span<ElementType, Extent>, false>;
  using const_iterator =
    span_details::span_iterator<Span<ElementType, Extent>, true>;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static const index_type extent = Extent;

  // [Span.cons], Span constructors, copy, assignment, and destructor
  // "Dependent" is needed to make "span_details::enable_if_t<(Dependent || Extent == 0 || Extent == mozilla::MaxValue<size_t>::value)>" SFINAE,
  // since "span_details::enable_if_t<(Extent == 0 || Extent == mozilla::MaxValue<size_t>::value)>" is ill-formed when Extent is neither of the extreme values.
  /**
   * Constructor with no args.
   */
  template<
    bool Dependent = false,
    class = span_details::enable_if_t<
      (Dependent || Extent == 0 || Extent == mozilla::MaxValue<size_t>::value)>>
  Span()
    : storage_(nullptr, span_details::extent_type<0>())
  {
  }

  /**
   * Constructor for nullptr.
   */
  MOZ_IMPLICIT Span(std::nullptr_t) : Span() {}

  /**
   * Constructor for pointer and length.
   */
  Span(pointer aPtr, index_type aLength)
    : storage_(aPtr, aLength)
  {
  }

  /**
   * Constructor for start pointer and pointer past end.
   */
  Span(pointer aStartPtr, pointer aEndPtr)
    : storage_(aStartPtr, std::distance(aStartPtr, aEndPtr))
  {
  }

  /**
   * Constructor for C array.
   */
  template<size_t N>
  MOZ_IMPLICIT Span(element_type (&aArr)[N])
    : storage_(&aArr[0], span_details::extent_type<N>())
  {
  }

  /**
   * Constructor for std::array.
   */
  template<size_t N,
           class ArrayElementType = span_details::remove_const_t<element_type>>
  MOZ_IMPLICIT Span(std::array<ArrayElementType, N>& aArr)
    : storage_(&aArr[0], span_details::extent_type<N>())
  {
  }

  /**
   * Constructor for const std::array.
   */
  template<size_t N>
  MOZ_IMPLICIT Span(
    const std::array<span_details::remove_const_t<element_type>, N>& aArr)
    : storage_(&aArr[0], span_details::extent_type<N>())
  {
  }

  /**
   * Constructor for mozilla::Array.
   */
  template<size_t N,
           class ArrayElementType = span_details::remove_const_t<element_type>>
  MOZ_IMPLICIT Span(mozilla::Array<ArrayElementType, N>& aArr)
    : storage_(&aArr[0], span_details::extent_type<N>())
  {
  }

  /**
   * Constructor for const mozilla::Array.
   */
  template<size_t N>
  MOZ_IMPLICIT Span(
    const mozilla::Array<span_details::remove_const_t<element_type>, N>& aArr)
    : storage_(&aArr[0], span_details::extent_type<N>())
  {
  }

  /**
   * Constructor for mozilla::UniquePtr holding an array and length.
   */
  template<class ArrayElementType = std::add_pointer<element_type>>
  Span(const mozilla::UniquePtr<ArrayElementType>& aPtr,
                 index_type aLength)
    : storage_(aPtr.get(), aLength)
  {
  }

  // NB: the SFINAE here uses .data() as a incomplete/imperfect proxy for the requirement
  // on Container to be a contiguous sequence container.
  /**
   * Constructor for standard-library containers.
   */
  template<
    class Container,
    class = span_details::enable_if_t<
      !span_details::is_span<Container>::value &&
      !span_details::is_std_array<Container>::value &&
      mozilla::IsConvertible<typename Container::pointer, pointer>::value &&
      mozilla::IsConvertible<typename Container::pointer,
                          decltype(mozilla::DeclVal<Container>().data())>::value>>
  MOZ_IMPLICIT Span(Container& cont)
    : Span(cont.data(), ReleaseAssertedCast<index_type>(cont.size()))
  {
  }

  /**
   * Constructor for standard-library containers (const version).
   */
  template<
    class Container,
    class = span_details::enable_if_t<
      mozilla::IsConst<element_type>::value &&
      !span_details::is_span<Container>::value &&
      mozilla::IsConvertible<typename Container::pointer, pointer>::value &&
      mozilla::IsConvertible<typename Container::pointer,
                          decltype(mozilla::DeclVal<Container>().data())>::value>>
  MOZ_IMPLICIT Span(const Container& cont)
    : Span(cont.data(), ReleaseAssertedCast<index_type>(cont.size()))
  {
  }

  /**
   * Constructor from other Span.
   */
  Span(const Span& other) = default;

  /**
   * Constructor from other Span.
   */
  Span(Span&& other) = default;

  /**
   * Constructor from other Span with conversion of element type.
   */
  template<
    class OtherElementType,
    size_t OtherExtent,
    class = span_details::enable_if_t<
      span_details::is_allowed_extent_conversion<OtherExtent, Extent>::value &&
      span_details::is_allowed_element_type_conversion<OtherElementType,
                                                       element_type>::value>>
  MOZ_IMPLICIT Span(const Span<OtherElementType, OtherExtent>& other)
    : storage_(other.data(),
               span_details::extent_type<OtherExtent>(other.size()))
  {
  }

  /**
   * Constructor from other Span with conversion of element type.
   */
  template<
    class OtherElementType,
    size_t OtherExtent,
    class = span_details::enable_if_t<
      span_details::is_allowed_extent_conversion<OtherExtent, Extent>::value &&
      span_details::is_allowed_element_type_conversion<OtherElementType,
                                                       element_type>::value>>
  MOZ_IMPLICIT Span(Span<OtherElementType, OtherExtent>&& other)
    : storage_(other.data(),
               span_details::extent_type<OtherExtent>(other.size()))
  {
  }

  ~Span() = default;
  Span& operator=(const Span& other)
    = default;

  Span& operator=(Span&& other)
    = default;

  // [Span.sub], Span subviews
  /**
   * Subspan with first N elements with compile-time N.
   */
  template<size_t Count>
  Span<element_type, Count> First() const
  {
    MOZ_RELEASE_ASSERT(Count <= size());
    return { data(), Count };
  }

  /**
   * Subspan with last N elements with compile-time N.
   */
  template<size_t Count>
  Span<element_type, Count> Last() const
  {
    MOZ_RELEASE_ASSERT(Count <= size());
    return { data() + (size() - Count), Count };
  }

  /**
   * Subspan with compile-time start index and length.
   */
  template<size_t Offset, size_t Count = dynamic_extent>
  Span<element_type, Count> Subspan() const
  {
    MOZ_RELEASE_ASSERT(Offset <= size() &&
      (Count == dynamic_extent || (Offset + Count <= size())));
    return { data() + Offset,
             Count == dynamic_extent ? size() - Offset : Count };
  }

  /**
   * Subspan with first N elements with run-time N.
   */
  Span<element_type, dynamic_extent> First(
    index_type aCount) const
  {
    MOZ_RELEASE_ASSERT(aCount <= size());
    return { data(), aCount };
  }

  /**
   * Subspan with last N elements with run-time N.
   */
  Span<element_type, dynamic_extent> Last(
    index_type aCount) const
  {
    MOZ_RELEASE_ASSERT(aCount <= size());
    return { data() + (size() - aCount), aCount };
  }

  /**
   * Subspan with run-time start index and length.
   */
  Span<element_type, dynamic_extent> Subspan(
    index_type aStart,
    index_type aLength = dynamic_extent) const
  {
    MOZ_RELEASE_ASSERT(aStart <= size() &&
                       (aLength == dynamic_extent ||
                        (aStart + aLength <= size())));
    return { data() + aStart,
             aLength == dynamic_extent ? size() - aStart : aLength };
  }

  /**
   * Subspan with run-time start index. (Rust's &foo[start..])
   */
  Span<element_type, dynamic_extent> From(index_type aStart) const
  {
    return Subspan(aStart);
  }

  /**
   * Subspan with run-time exclusive end index. (Rust's &foo[..end])
   */
  Span<element_type, dynamic_extent> To(index_type aEnd) const
  {
    return Subspan(0, aEnd);
  }

  /**
   * Subspan with run-time start index and exclusive end index.
   * (Rust's &foo[start..end])
   */
  Span<element_type, dynamic_extent> FromTo(index_type aStart, index_type aEnd) const
  {
    MOZ_RELEASE_ASSERT(aStart <= aEnd);
    return Subspan(aStart, aEnd - aStart);
  }

  // [Span.obs], Span observers
  /**
   * Number of elements in the span.
   */
  index_type Length() const { return size(); }

  /**
   * Number of elements in the span (standard-libray duck typing version).
   */
  index_type size() const { return storage_.size(); }

  /**
   * Size of the span in bytes.
   */
  index_type LengthBytes() const { return size_bytes(); }

  /**
   * Size of the span in bytes (standard-library naming style version).
   */
  index_type size_bytes() const
  {
    return size() * narrow_cast<index_type>(sizeof(element_type));
  }

  /**
   * Checks if the the length of the span is zero.
   */
  bool IsEmpty() const { return empty(); }

  /**
   * Checks if the the length of the span is zero (standard-libray duck
   * typing version).
   */
  bool empty() const { return size() == 0; }

  // [Span.elem], Span element access
  reference operator[](index_type idx) const
  {
    MOZ_RELEASE_ASSERT(idx < storage_.size());
    return data()[idx];
  }

  /**
   * Access element of span by index (standard-library duck typing version).
   */
  reference at(index_type idx) const { return this->operator[](idx); }

  reference operator()(index_type idx) const
  {
    return this->operator[](idx);
  }

  /**
   * Pointer to the first element of the span.
   */
  pointer Elements() const { return data(); }

  /**
   * Pointer to the first element of the span (standard-libray duck typing version).
   */
  pointer data() const { return storage_.data(); }

  // [Span.iter], Span iterator support
  iterator begin() const { return { this, 0 }; }
  iterator end() const { return { this, Length() }; }

  const_iterator cbegin() const { return { this, 0 }; }
  const_iterator cend() const { return { this, Length() }; }

  reverse_iterator rbegin() const
  {
    return reverse_iterator{ end() };
  }
  reverse_iterator rend() const
  {
    return reverse_iterator{ begin() };
  }

  const_reverse_iterator crbegin() const
  {
    return const_reverse_iterator{ cend() };
  }
  const_reverse_iterator crend() const
  {
    return const_reverse_iterator{ cbegin() };
  }

private:
  // this implementation detail class lets us take advantage of the
  // empty base class optimization to pay for only storage of a single
  // pointer in the case of fixed-size Spans
  template<class ExtentType>
  class storage_type : public ExtentType
  {
  public:
    template<class OtherExtentType>
    storage_type(pointer elements, OtherExtentType ext)
      : ExtentType(ext)
      , data_(elements)
    {
      MOZ_RELEASE_ASSERT(
        (!elements && ExtentType::size() == 0) ||
        (elements && ExtentType::size() != mozilla::MaxValue<size_t>::value));
    }

    pointer data() const { return data_; }

  private:
    pointer data_;
  };

  storage_type<span_details::extent_type<Extent>> storage_;
};

// [Span.comparison], Span comparison operators
template<class ElementType, size_t FirstExtent, size_t SecondExtent>
inline bool
operator==(const Span<ElementType, FirstExtent>& l,
           const Span<ElementType, SecondExtent>& r)
{
  return (l.size() == r.size()) && std::equal(l.begin(), l.end(), r.begin());
}

template<class ElementType, size_t Extent>
inline bool
operator!=(const Span<ElementType, Extent>& l,
           const Span<ElementType, Extent>& r)
{
  return !(l == r);
}

template<class ElementType, size_t Extent>
inline bool
operator<(const Span<ElementType, Extent>& l,
          const Span<ElementType, Extent>& r)
{
  return std::lexicographical_compare(l.begin(), l.end(), r.begin(), r.end());
}

template<class ElementType, size_t Extent>
inline bool
operator<=(const Span<ElementType, Extent>& l,
           const Span<ElementType, Extent>& r)
{
  return !(l > r);
}

template<class ElementType, size_t Extent>
inline bool
operator>(const Span<ElementType, Extent>& l,
          const Span<ElementType, Extent>& r)
{
  return r < l;
}

template<class ElementType, size_t Extent>
inline bool
operator>=(const Span<ElementType, Extent>& l,
           const Span<ElementType, Extent>& r)
{
  return !(l < r);
}

namespace span_details {
template<class ElementType, size_t Extent>
struct calculate_byte_size
  : mozilla::IntegralConstant<size_t,
                           static_cast<size_t>(sizeof(ElementType) *
                                               static_cast<size_t>(Extent))>
{
};

template<class ElementType>
struct calculate_byte_size<ElementType, dynamic_extent>
  : mozilla::IntegralConstant<size_t, dynamic_extent>
{
};
}

// [Span.objectrep], views of object representation
/**
 * View span as Span<const uint8_t>.
 */
template<class ElementType, size_t Extent>
Span<const uint8_t,
     span_details::calculate_byte_size<ElementType, Extent>::value>
AsBytes(Span<ElementType, Extent> s)
{
  return { reinterpret_cast<const uint8_t*>(s.data()), s.size_bytes() };
}

/**
 * View span as Span<uint8_t>.
 */
template<class ElementType,
         size_t Extent,
         class = span_details::enable_if_t<!mozilla::IsConst<ElementType>::value>>
Span<uint8_t, span_details::calculate_byte_size<ElementType, Extent>::value>
AsWritableBytes(Span<ElementType, Extent> s)
{
  return { reinterpret_cast<uint8_t*>(s.data()), s.size_bytes() };
}

//
// MakeSpan() - Utility functions for creating Spans
//
/**
 * Create span from pointer and length.
 */
template<class ElementType>
Span<ElementType>
MakeSpan(ElementType* aPtr, typename Span<ElementType>::index_type aLength)
{
  return Span<ElementType>(aPtr, aLength);
}

/**
 * Create span from start pointer and pointer past end.
 */
template<class ElementType>
Span<ElementType>
MakeSpan(ElementType* aStartPtr, ElementType* aEndPtr)
{
  return Span<ElementType>(aStartPtr, aEndPtr);
}

/**
 * Create span from C array.
 */
template<class ElementType, size_t N>
Span<ElementType> MakeSpan(ElementType (&aArr)[N])
{
  return Span<ElementType>(aArr);
}

/**
 * Create span from mozilla::Array.
 */
template<class ElementType, size_t N>
Span<ElementType>
MakeSpan(mozilla::Array<ElementType, N>& aArr)
{
  return aArr;
}

/**
 * Create span from const mozilla::Array.
 */
template<class ElementType, size_t N>
Span<const ElementType>
MakeSpan(const mozilla::Array<ElementType, N>& arr)
{
  return arr;
}

/**
 * Create span from standard-library container.
 */
template<class Container>
Span<typename Container::value_type>
MakeSpan(Container& cont)
{
  return Span<typename Container::value_type>(cont);
}

/**
 * Create span from standard-library container (const version).
 */
template<class Container>
Span<const typename Container::value_type>
MakeSpan(const Container& cont)
{
  return Span<const typename Container::value_type>(cont);
}

/**
 * Create span from smart pointer and length.
 */
template<class Ptr>
Span<typename Ptr::element_type>
MakeSpan(Ptr& aPtr, size_t aLength)
{
  return Span<typename Ptr::element_type>(aPtr, aLength);
}

/**
 * Create span from C string.
 */
inline Span<const char>
MakeCStringSpan(const char* aStr)
{
  return Span<const char>(aStr, std::strlen(aStr));
}

} // namespace mozilla

#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#endif // mozilla_Span_h
